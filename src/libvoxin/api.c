#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include "eci.h"
#include "debug.h"
#include "conf.h"
#include "libvoxin.h"
#include "msg.h"

#define __USE_GNU
#include <sys/time.h>


#define ENGINE_ID 0x020A0005
#define IS_ENGINE(e) (e && (e->id == ENGINE_ID) && e->handle && e->api)

#define IS_OBJECT_VALID(api, birth) (api				\
				     && timercmp(&(birth),		\
						 &api->child_birth, >))

#define NB_PARAMS (eciNumParams+1)
#define NB_VOICES (eciNumVoiceParams+ECI_USER_DEFINED_VOICES)
#define NB_VOICE_PARAMS (eciNumVoiceParams+1)
#define DICTIONARY_ID 0x030A0005
#define IS_DICTIONARY(d) (d && (d->id == DICTIONARY_ID) && d->handle && d->engine)

const struct timeval NO_BIRTH = {0, 0};

const uint32_t lang_id[] = {
  eciGeneralAmericanEnglish,
  eciBritishEnglish,
  eciCastilianSpanish,
  eciMexicanSpanish,
  eciStandardFrench,
  eciCanadianFrench,
  eciStandardGerman,
  eciStandardItalian,
  eciMandarinChinese,
  eciMandarinChineseGB,
  eciMandarinChinesePinYin,
  eciMandarinChineseUCS,
  eciTaiwaneseMandarin,
  eciTaiwaneseMandarinBig5,
  eciTaiwaneseMandarinZhuYin,
  eciTaiwaneseMandarinPinYin,
  eciTaiwaneseMandarinUCS,
  eciBrazilianPortuguese,
  eciStandardJapanese,
  eciStandardJapaneseSJIS,
  eciStandardJapaneseUCS,
  eciStandardFinnish,
  eciStandardCantonese,
  eciStandardCantoneseGB,
  eciStandardCantoneseUCS,
  eciHongKongCantonese,
  eciHongKongCantoneseBig5,
  eciHongKongCantoneseUCS,
};
#define NB_LANG (sizeof(lang_id)/sizeof(lang_id[0]))
#define NB_DICT_SET 4 // Currently only 4 dictionary sets are managed for each language
#define NB_DICT_VOLUME 4


struct engine_t {
  uint32_t id; // structure identifier
  struct timeval birth;
  struct api_t *api; // parent api
  uint32_t handle; // eci handle
  bool stop_required;
  // from eciRegisterCallback
  void *cb; // user callback
  void *data_cb; // user data callback
  // from eciSetOutputBufffer
  int16_t *samples; // user samples buffer
  uint32_t nb_samples; // current number of samples in the user sample buffer
  // from eciSetOutputFilename
  char* output_filename;
  // from eciSetParam
  uint32_t* param[NB_PARAMS]; // if NULL: default, else pointer to priv_param
  uint32_t priv_param[NB_PARAMS];
  // from eciSetVoiceParam
  uint32_t* voice_param[NB_VOICES][NB_VOICE_PARAMS]; // if NULL: default, else pointer to priv_voice_param
  uint32_t priv_voice_param[NB_VOICES][NB_VOICE_PARAMS];
  struct dictionary_t *dictionary_set[NB_LANG][NB_DICT_SET];
  //TODO  char* voice_name[NB_VOICES][ECI_VOICE_NAME_LENGTH+2]; // from eciSetVoiceName
};

struct dictionary_t {
  uint32_t id; // structure identifier
  struct timeval birth;
  // from eciNewDict
  struct engine_t *engine; // parent engine
  uint32_t handle; // eci dict handle
  uint32_t lang; // lang_id index
  // from eciLoadDict
  char *filename[NB_DICT_VOLUME];
  // from eciSetDict
  bool is_set;
};

#define ALLOCATED_MSG_LENGTH PIPE_MAX_BLOCK

struct api_t {
  libvoxin_handle_t my_instance; // communication channel with voxind. my_api is fully created when my_instance is non NULL
  pthread_mutex_t stop_mutex; // to process only one stop command
  pthread_mutex_t api_mutex; // to process exclusively any other command
  uint32_t* default_param[NB_PARAMS]; // if NULL: default value, otherwise pointer to priv_default_param; from eciSetDefaultParam
  uint32_t priv_default_param[NB_PARAMS];
  struct msg_t *msg; // message for voxind
  struct timeval child_birth;
};

static struct api_t my_api = {0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, NULL};

static int api_send_command(struct api_t* api, struct msg_t *header, const char* data, int *eci_res);
static int engine_restore(struct engine_t *engine);

static int get_timestamp(struct timeval *tv)
{
  struct timespec ts;

  ENTER();

  if (!tv)
    return EINVAL;
  if (!clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
    TIMESPEC_TO_TIMEVAL(tv, &ts);
  } else {
    gettimeofday(tv, NULL);
  }
  msg("t=%lus.%lu", tv->tv_sec, tv->tv_usec);
  return 0;
}

static int api_create(struct api_t *api)
{
  int res = 0;

  ENTER();

  if (!api) {
    err("LEAVE, args error");
    return EINVAL;
  }

  if (api->my_instance) {
    err("LEAVE, error already created");
    return EINVAL;
  }

  res = pthread_mutex_lock(&api->api_mutex);
  if (res) {
    err("LEAVE, api_mutex error l (%d)", res);
    return res;
  }

  res = pthread_mutex_lock(&api->stop_mutex);
  if (res) {
    pthread_mutex_unlock(&api->api_mutex);
    err("LEAVE, stop_mutex error l (%d)", res);
    return res;
  }

  BUILD_ASSERT(PIPE_MAX_BLOCK > MSG_HEADER_LENGTH);
  api->msg = calloc(1, PIPE_MAX_BLOCK);
  if (!api->msg) {
    res = errno;
    goto exit0;
  }

  res = libvoxin_create(&api->my_instance);

 exit0:
  if (res) {
    err("error (%d)", res);
    if (api->msg)
      free(api->msg);
  }
  {
    int res;
    get_timestamp(&api->child_birth);
    res = pthread_mutex_unlock(&api->api_mutex);
    if (res) {
      err("api_mutex error u (%d)", res);
    }
    res = pthread_mutex_unlock(&api->stop_mutex);
    if (res) {
      err("stop_mutex error u (%d)", res);
    }
  }
  LEAVE();
  return res;
}


static int msg_set_header(struct msg_t *msg, uint32_t func, uint32_t engine_handle)
{
  int res = 0;

  if (!msg) {
    err("LEAVE, args error");
    return EINVAL;
  }

  memset(msg, 0, MSG_HEADER_LENGTH);
  msg->id = MSG_TO_ECI_ID;
  msg->func = func;
  msg->engine = engine_handle;
  return 0;
}

#define MSG_UPDATE_HEADER(msg, engine_handle) (msg && (msg->engine = engine_handle))

static int msg_copy_data(struct msg_t *msg, const char *text)
{
  char *c = NULL;
  int res = 0;
  int effective_msg_length;

  ENTER();
  if (!msg || !text) {
    err("LEAVE, args error 0");
    return EINVAL;
  }

  c = memccpy(msg->data, text, 0, ALLOCATED_MSG_LENGTH - MSG_HEADER_LENGTH - 1);
  if (c == NULL) {
    err("LEAVE, args error 1");
    return EINVAL;
  }

  dbg("text=%s", text);

  effective_msg_length = (size_t)((char*)c - (char*)msg);
  msg->effective_data_length = effective_msg_length - MSG_HEADER_LENGTH;
  dbg("data length=%d", msg->effective_data_length);

  LEAVE();
  return 0;
}


static int api_lock(struct api_t *api)
{
  int res;

  ENTER();

  if (!api) {
    err("LEAVE, args error");
    return EINVAL;
  }

  if (!api->my_instance) {
    res = api_create(api);
    if (res)
      return res;
  }

  res = pthread_mutex_lock(&api->api_mutex);
  if (res) {
    err("LEAVE, api_mutex error l (%d)", res);
  }

  if (timercmp(&api->child_birth, &NO_BIRTH, ==)) {
    int i;

    msg("new child process");
    res = libvoxin_delete(&api->my_instance);
    if (res)
      goto exit0;
    res = libvoxin_create(&api->my_instance);
    if (res)
      goto exit0;

    dbg("replay tts setup");
    for (i=0; i<NB_PARAMS; i++) {
      if (api->default_param[i]) {
	struct msg_t header;
	int eci_res;
	msg_set_header(&header, MSG_SET_DEFAULT_PARAM, 0);
	header.args.sp.Param = i;
	header.args.sp.iValue = *(api->default_param[i]);
	dbg("set default_param[%d]: %d", i, *(api->default_param[i]));
	if (api_send_command(api, &header, NULL, &eci_res) || !eci_res)
	  goto exit0;
      }
    }
    get_timestamp(&api->child_birth);
  }

 exit0:
  LEAVE();
  return res;
}


static int api_unlock(struct api_t *api)
{
  int res = 0;

  ENTER();

  if (!api) {
    err("LEAVE, args error");
    return EINVAL;
  }

  res = pthread_mutex_unlock(&api->api_mutex);
  if (res) {
    err("api_mutex error u (%d)", res);
  }

  LEAVE();
  return res;
}

static int api_send_command(struct api_t* api, struct msg_t *header, const char* data,
			    int *eci_res)
{
  int res = EINVAL;
  uint32_t c;

  ENTER();

  if (!api || !header) {
    err("LEAVE, args error");
    return res;
  }

  c = api->msg->count;
  memcpy(api->msg, header, sizeof(*api->msg));
  api->msg->count = c;

  if (data) {
    res = msg_copy_data(api->msg, data);
    if (res)
      goto exit0;
  }
  api->msg->allocated_data_length = ALLOCATED_MSG_LENGTH;

  res = libvoxin_call_tts(api->my_instance, api->msg);

 exit0:
  if (res == ECHILD) {
    api->child_birth = NO_BIRTH;
  } else if (!res && eci_res)
    *eci_res = api->msg->res;

  LEAVE();
  return res;
}


static int engine_send_command(struct engine_t *engine, struct msg_t *header,
			       const char* data, int *eci_res)
{
  int res = EINVAL;

  ENTER();

  if (!engine) {
    err("LEAVE, args error");
    return res;
  }

  if (!IS_OBJECT_VALID(engine->api, engine->birth)) {
    res = engine_restore(engine);
    if (res)
      goto exit0;
    MSG_UPDATE_HEADER(header, engine->handle);
  }

  res = api_send_command(engine->api, header, data, eci_res);

 exit0:
  LEAVE();
  return res;
}


static int api_set_birth(struct api_t *api, struct timeval *birth)
{
  int res = EIO;

  ENTER();

  if (api && birth) {
    struct timeval tv;
    get_timestamp(&tv);
    if (timercmp(&tv, &api->child_birth, ==)) {
      while((usleep(1) == -1) && (errno == EINTR))
	{}
      get_timestamp(&tv);
    }
    if (timercmp(&tv, &api->child_birth, >)) {
      *birth = tv;
      res = 0;
    }
  } else {
    err("LEAVE, args error");
    res = EINVAL;
  }

  LEAVE();
  return res;
}

static struct engine_t *engine_create(uint32_t handle, struct api_t *api)
{
  struct engine_t *engine = NULL;
  int res;

  ENTER();

  engine = calloc(1, sizeof(*engine));
  if (!engine) {
    err("mem error (%d)", errno);
  } else {
    engine->id = ENGINE_ID;
    engine->handle = handle;
    engine->api = api;

    if (api_set_birth(api, &engine->birth)) {
      free(engine);
      engine = NULL;
    }
  }

  LEAVE();
  return engine;
}


static struct engine_t *engine_delete(struct engine_t *engine)
{
  int res;

  ENTER();

  if (!engine)
    return NULL;

  free(engine->output_filename);
  memset(engine, 0, sizeof(*engine));
  free(engine);
  engine = NULL;

  LEAVE();
  return engine;
}

/* must be called with mutex api locked */
static int engine_restore(struct engine_t *engine)
{
  int res = EIO;
  struct msg_t header;
  int eci_res = -1;
  int i, j;

  ENTER();
  if (!engine || !engine->api) {
    err("LEAVE, args error");
    res = EINVAL;
  }

  dbg("new engine");
  msg_set_header(&header, MSG_NEW, 0);
  if (api_send_command(engine->api, &header, NULL, &eci_res) || (eci_res == 0))
    goto exit0;

  engine->handle = eci_res;

  if (api_set_birth(engine->api, &engine->birth))
    goto exit0;

  if (engine->cb) {
    dbg("restore cb");
    msg_set_header(&header, MSG_REGISTER_CALLBACK, engine->handle);
    header.args.rc.Callback = !!engine->cb;
    if (api_send_command(engine->api, &header, NULL, NULL))
      goto exit0;
  }

  if (engine->samples) {
    dbg("set output buffer");
    msg_set_header(&header, MSG_SET_OUTPUT_BUFFER, engine->handle);
    header.args.sob.nb_samples = engine->nb_samples;
    if (api_send_command(engine->api, &header, NULL, &eci_res) || !eci_res)
      goto exit0;
  }

  for (i=0; i<NB_PARAMS; i++) {
    if (engine->param[i]) {
      msg_set_header(&header, MSG_SET_PARAM, engine->handle);
      header.args.sp.Param = i;
      header.args.sp.iValue = *engine->param[i];
      if (api_send_command(engine->api, &header, NULL, &eci_res) || !eci_res)
	goto exit0;
    }
  }

  for (i=0; i<NB_VOICES; i++) {
    for (j=0; j<NB_VOICE_PARAMS; j++) {
      if (engine->voice_param[i][j]) {
	msg_set_header(&header, MSG_SET_VOICE_PARAM, engine->handle);
	header.args.svp.iVoice = i;
	header.args.svp.Param = j;
	header.args.svp.iValue = *engine->voice_param[i][j];
	if (api_send_command(engine->api, &header, NULL, &eci_res) || !eci_res)
	  goto exit0;
      }
    }
  }

  // TODO restore dictionaries
  
  // TODO set expected default language
  
  // TODO filter
  // TODO engine->output_filename = pFilename;

  res = 0;

 exit0:
  LEAVE();
  return res;
}


ECIHand eciNew(void)
{
  int eci_res;
  struct engine_t *engine = NULL;
  struct msg_t header;
  struct api_t *api = &my_api;

  ENTER();

  if (msg_set_header(&header, MSG_NEW, 0))
    return NULL;

  if (!api_lock(api)) {
    if (!api_send_command(api, &header, NULL, &eci_res) && eci_res)
      engine = engine_create(eci_res, api);
    api_unlock(api);
  }

  dbg("LEAVE, engine=%p", engine);
  return (ECIHand)engine;
}


Boolean eciSetOutputBuffer(ECIHand hEngine, int iSize, short *psBuffer)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct api_t *api;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  api = engine->api;
  if (msg_set_header(&header, MSG_SET_OUTPUT_BUFFER, engine->handle))
    goto exit0;

  header.args.sob.nb_samples = iSize;

  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, &eci_res) && eci_res) {
      engine->samples = psBuffer;
      engine->nb_samples = iSize;
    }
    api_unlock(engine->api);
  }

 exit0:
  dbg("LEAVE, eci_res=%d", eci_res);
  return eci_res;
}


Boolean eciSetOutputFilename(ECIHand hEngine, const void *pFilename)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECIFalse;
  }

  msg_set_header(&header, MSG_SET_OUTPUT_FILENAME, engine->handle);
  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, pFilename, &eci_res) && eci_res)
      engine->output_filename = strdup(pFilename);
    api_unlock(engine->api);
  }
  return eci_res;
}


Boolean eciAddText(ECIHand hEngine, ECIInputText pText)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECIFalse;
  }

  msg_set_header(&header, MSG_ADD_TEXT, engine->handle);
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, pText, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


Boolean eciSynthesize(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECIFalse;
  }

  msg_set_header(&header, MSG_SYNTHESIZE, engine->handle);
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


static Boolean synchronize(struct engine_t *engine, enum msg_type type)
{
  Boolean eci_res = ECIFalse;
  int res;
  struct msg_t *m = NULL;
  struct api_t *api;
  uint32_t c;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  api = engine->api;

  res = pthread_mutex_lock(&api->api_mutex);
  if (res) {
    err("LEAVE, api_mutex error l (%d)", res);
    return eci_res;
  }

  m = api->msg;
  c = m->count;
  msg_set_header(m, type, engine->handle);
  m->count = c;
  m->allocated_data_length = ALLOCATED_MSG_LENGTH;
  res = libvoxin_call_tts(api->my_instance, m);
  if (res)
    goto exit0;

  while(1) {
    if (m->func < MSG_CB_WAVEFORM_BUFFER)
      break;

    m->res = eciDataAbort;
    if (engine->cb && engine->samples
	&& (m->effective_data_length <= 2*engine->nb_samples)) {
      ECICallback cb = engine->cb;
      enum ECIMessage Msg = m->func - MSG_CB_WAVEFORM_BUFFER + eciWaveformBuffer;
      dbg("call user callback, handle=0x%x, msg=%s, #samples=%d",
	  engine->handle, msg_string(m->func), m->effective_data_length/2);
      memcpy(engine->samples, m->data, m->effective_data_length);
      m->res = (enum ECICallbackReturn)cb((ECIHand)((char*)NULL+engine->handle),
					  Msg, m->effective_data_length/2,
					  engine->data_cb);
    } else {
      err("error callback, handle=0x%x, msg=%s, #samples=%d",
	  engine->handle, msg_string(m->func), m->effective_data_length/2);
    }

    dbg("res user callback=%d", m->res);
    if (engine->stop_required) {
      m->res = eciDataAbort;
      dbg("stop required");
    }

    m->id = MSG_TO_ECI_ID;
    m->allocated_data_length = ALLOCATED_MSG_LENGTH;
    res = libvoxin_call_tts(api->my_instance, m);
    if (res)
      goto exit0;
  }

 exit0:
  if (!res) {
    eci_res =  m->res;
  } else if (res == ECHILD) {
    api->child_birth = NO_BIRTH;
  }

  res = pthread_mutex_unlock(&api->api_mutex);
  if (res) {
    err("api_mutex error u (%d)", res);
  }

  dbg("LEAVE(eci_res=0x%x)",eci_res);
  return eci_res;
}


Boolean eciSynchronize(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct api_t *api;
  int res;

  ENTER();

  eci_res = synchronize(hEngine, MSG_SYNCHRONIZE);

  LEAVE();
  return eci_res;
}


ECIHand eciDelete(ECIHand hEngine)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  ECIHand eci_res = hEngine;
  struct api_t *api;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("args error");
    goto exit0;
  }

  api = engine->api;
  if (api_lock(engine->api))
    goto exit0;

  if (!IS_OBJECT_VALID(engine->api, engine->birth)) {
    err("engine invalid");
    goto exit0;
  }

  msg_set_header(&header, MSG_DELETE, engine->handle);
  if (!engine_send_command(engine, &header, NULL, (int*)&eci_res) && !eci_res) {
    eci_res = engine_delete(engine);
  }
  api_unlock(api);

 exit0:
  LEAVE();
  return eci_res;
}


void eciRegisterCallback(ECIHand hEngine, ECICallback Callback, void *pData)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return;
  }

  msg_set_header(&header, MSG_REGISTER_CALLBACK, engine->handle);
  header.args.rc.Callback = !!Callback;

  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, NULL)) {
      engine->cb = (void*)Callback;
      engine->data_cb = pData;
    }
    api_unlock(engine->api);
  }

  LEAVE();
}


Boolean eciSpeaking(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  int res;
  struct api_t *api;

  ENTER();

  eci_res = synchronize(hEngine, MSG_SPEAKING);

  LEAVE();
  return eci_res;
}


Boolean eciStop(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct api_t *api;
  int res;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error 0");
    return eci_res;
  }

  api = engine->api;
  if (!api->my_instance) {
    err("LEAVE, args error 1");
    return eci_res;
  }

  res = pthread_mutex_lock(&api->stop_mutex);
  if (res) {
    err("LEAVE, stop_mutex error l (%d)", res);
    return eci_res;
  }

  engine->stop_required = true;

  msg_set_header(&header, MSG_STOP, engine->handle);
  if (!api_lock(engine->api)) {
    if (IS_OBJECT_VALID(engine->api, engine->birth))
      engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }

  engine->stop_required = false;
  res = pthread_mutex_unlock(&api->stop_mutex);
  if (res) {
    err("LEAVE, stop_mutex error u (%d)", res);
  }
  LEAVE();
  return eci_res;
}


int eciGetAvailableLanguages(enum ECILanguageDialect *aLanguages, int *nLanguages)
{
  int eci_res = ECI_PARAMETERERROR;
  struct msg_t header;
  struct api_t *api = &my_api;

  ENTER();

  if (!aLanguages || !nLanguages) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_GET_AVAILABLE_LANGUAGES, 0);
  if (!api_lock(api)) {
    if (!api_send_command(api, &header, NULL, &eci_res)) {
      struct msg_get_available_languages_t *lang =
	(struct msg_get_available_languages_t *)api->msg->data;
      msg("nb lang=%d", lang->nb);
      if (lang->nb <= MSG_LANG_INFO_MAX) {
	int i;
	eci_res =  0;
	*nLanguages = lang->nb;
	for (i=0; i<lang->nb; i++) {
	  aLanguages[i] = lang->languages[i];
	  msg("lang[%d]=0x%x", i, aLanguages[i]);
	}
      }
    }
    api_unlock(api);
  }
  return eci_res;
}


ECIHand eciNewEx(enum ECILanguageDialect Value)
{
  int eci_res;
  struct engine_t *engine = NULL;
  struct msg_t header;
  struct api_t *api = &my_api;

  ENTER();

  if (msg_set_header(&header, MSG_NEW_EX, 0))
    return NULL;

  header.args.ne.Value = Value;

  if (!api_lock(api)) {
    if (!api_send_command(api, &header, NULL, &eci_res) && eci_res) {
      engine = engine_create(eci_res, api);
    }
    api_unlock(api);
  }

  dbg("LEAVE, engine=%p", engine);
  return (ECIHand)engine;
}


int eciSetParam(ECIHand hEngine, enum ECIParam Param, int iValue)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct api_t *api;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_SET_PARAM, engine->handle);
  header.args.sp.Param = Param;
  header.args.sp.iValue = iValue;
  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, &eci_res)
	&& eci_res && (Param < NB_PARAMS)) {
      engine->priv_param[Param] = iValue;
      engine->param[Param] = &engine->priv_param[Param];
    }
    api_unlock(engine->api);
  }
  return eci_res;
}

int eciSetDefaultParam(enum ECIParam parameter, int value)
{
  int eci_res = -1;
  struct api_t *api = &my_api;
  struct msg_t header;

  ENTER();

  msg_set_header(&header, MSG_SET_DEFAULT_PARAM, 0);
  header.args.sp.Param = parameter;
  header.args.sp.iValue = value;
  if (!api_lock(api)) {
    if (!api_send_command(api, &header, NULL, &eci_res)
	&& eci_res && (parameter < NB_PARAMS)) {
      api->priv_default_param[parameter] = value;
      api->default_param[parameter] = &api->priv_default_param[parameter];
    }
    api_unlock(api);
  }
  return eci_res;
}

int eciGetParam(ECIHand hEngine, enum ECIParam Param)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct api_t *api;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  api = engine->api;

  msg_set_header(&header, MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = Param;
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}

int eciGetDefaultParam(enum ECIParam parameter)
{
  int eci_res = -1;
  struct msg_t header;
  struct api_t *api = &my_api;

  ENTER();

  msg_set_header(&header, MSG_GET_DEFAULT_PARAM, 0);
  if (!api_lock(api)) {
    api_send_command(api, &header, NULL, &eci_res);
    api_unlock(api);
  }

  LEAVE();
  return eci_res;
}


void eciErrorMessage(ECIHand hEngine, void *buffer)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct api_t *api;

  ENTER();

  if (!buffer) {
    err("LEAVE, args error 0");
    return;
  }

  *(char*)buffer=0;

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error 1");
    return;
  }

  api = engine->api;
  msg_set_header(&header, MSG_ERROR_MESSAGE, engine->handle);
  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, NULL)) {
      memccpy(buffer, api->msg->data, 0, MSG_ERROR_MESSAGE);
      msg("msg=%s", (char*)buffer);
    }
    api_unlock(engine->api);
  }

  LEAVE();
}


int eciProgStatus(ECIHand hEngine)
{
  int eci_res = ECI_SYSTEMERROR;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECI_PARAMETERERROR;
  }

  msg_set_header(&header, MSG_PROG_STATUS, engine->handle);
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


void eciClearErrors(ECIHand hEngine)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  ENTER();
  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return;
  }
  msg_set_header(&header, MSG_CLEAR_ERRORS, engine->handle);
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, NULL);
    api_unlock(engine->api);
  }
}

Boolean eciReset(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  ENTER();
  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECIFalse;
  }
  msg_set_header(&header, MSG_RESET, engine->handle);

  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, &eci_res) && eci_res) {
      memset(engine->param, 0, sizeof(engine->param));
    }
    api_unlock(engine->api);
  }

  return eci_res;
}


void eciVersion(char *pBuffer)
{
  struct api_t *api = &my_api;
  struct msg_t header;

  ENTER();

  if (!pBuffer) {
    err("LEAVE, args error");
    return;
  }

  *(char*)pBuffer=0;

  msg_set_header(&header, MSG_VERSION, 0);
  if (!api_lock(api)){
    if (!api_send_command(api, &header, NULL, NULL)) {
      memccpy(pBuffer, api->msg->data, 0, MAX_VERSION);
      msg("version=%s", (char*)pBuffer);
    }
    api_unlock(api);
  }

  LEAVE();
}


int eciGetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_GET_VOICE_PARAM, engine->handle);
  header.args.gvp.iVoice = iVoice;
  header.args.gvp.Param = Param;
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


int eciSetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param, int iValue)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_SET_VOICE_PARAM, engine->handle);
  header.args.svp.iVoice = iVoice;
  header.args.svp.Param = Param;
  header.args.svp.iValue = iValue;

  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, &eci_res)
	&& eci_res && (iVoice < NB_VOICES) && (Param < NB_VOICE_PARAMS)) {
      engine->priv_voice_param[iVoice][Param] = iValue;
      engine->voice_param[iVoice][Param] = &engine->priv_voice_param[iVoice][Param];
    }
    api_unlock(engine->api);
  }

  return eci_res;
}


Boolean eciPause(ECIHand hEngine, Boolean On)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_PAUSE, engine->handle);
  header.args.p.On = On;
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


Boolean eciInsertIndex(ECIHand hEngine, int iIndex)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_INSERT_INDEX, engine->handle);
  header.args.ii.iIndex = iIndex;
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }

  return eci_res;
}

Boolean eciCopyVoice(ECIHand hEngine, int iVoiceFrom, int iVoiceTo)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_COPY_VOICE, engine->handle);
  header.args.cv.iVoiceFrom = iVoiceFrom;
  header.args.cv.iVoiceTo = iVoiceTo;

  if (!api_lock(engine->api)) {
    if (!engine_send_command(engine, &header, NULL, &eci_res)
	&& eci_res && (iVoiceFrom < NB_VOICES) && (iVoiceTo < NB_VOICES)) {
      memcpy(&engine->priv_voice_param[iVoiceTo],
	     &engine->priv_voice_param[iVoiceFrom],
	     sizeof(engine->priv_voice_param[iVoiceTo]));
      memcpy(&engine->voice_param[iVoiceTo],
	     &engine->priv_voice_param[iVoiceFrom],
	     sizeof(engine->voice_param[iVoiceTo]));
    }
    api_unlock(engine->api);
  }

  return eci_res;
}


ECIDictHand eciNewDict(ECIHand hEngine)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct dictionary_t *d = NULL;
  int res = 0;
  int i,j;

  ENTER();
  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return NULL_DICT_HAND;
  }

  if (api_lock(engine->api))
    return NULL_DICT_HAND;

  msg_set_header(&header, MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = eciLanguageDialect;
  if (engine_send_command(engine, &header, NULL, &res) || !res)
    goto exit0;

  for (i=0; i<NB_LANG; i++) {
    if (lang_id[i] == res) {
      break;
    }
  }
  if (i>=NB_LANG) {
    err("unknown language!");
    goto exit0;
  }
  
  for (j=0; j<NB_DICT_SET; j++) {
    if (!engine->dictionary_set[i][j]) {
      dbg("empty slot found: engine->dictionary_set[%d][%d]", i, j);
      msg_set_header(&header, MSG_NEW_DICT, engine->handle);
      if (engine_send_command(engine, &header, NULL, &res) || !res)
	goto exit0;

      d = calloc(1, sizeof(*d));
      d->lang = i;
      d->handle = res;
      d->engine = engine;
      api_set_birth(engine->api, &d->birth);
      d->id = DICTIONARY_ID;
      
      engine->dictionary_set[i][j] = d;
      break;
    }
  }

  if (j >= NB_DICT_SET) {
    msg("no empty slot");
    goto exit0;
  }

  eci_res = (ECIDictHand)engine->dictionary_set[i][j];

 exit0:
    api_unlock(engine->api);
    return eci_res;
}

ECIDictHand eciGetDict(ECIHand hEngine)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  uint32_t res;
  int i,j;

  ENTER();
  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return NULL_DICT_HAND;
  }

  if (api_lock(engine->api))
    return NULL_DICT_HAND;

  msg_set_header(&header, MSG_GET_DICT, engine->handle);
  if (engine_send_command(engine, &header, NULL, &res) || !res)
    goto exit0;

  for (i=0; i<NB_LANG; i++) {
    for (j=0; j<NB_DICT_SET; j++) {
      if (engine->dictionary_set[i][j] && (engine->dictionary_set[i][j]->handle == res)) {
	eci_res = engine->dictionary_set[i][j];
	dbg("slot found: engine->dictionary_set[%d][%d]=%p", i, j, eci_res);
	break;
      }
    }
  }

 exit0:
  api_unlock(engine->api);
  return eci_res;
}

enum ECIDictError eciSetDict(ECIHand hEngine, ECIDictHand hDict)
{
  enum ECIDictError eci_res = DictErrLookUpKey;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct dictionary_t *d = (struct dictionary_t *)hDict;
  int i,j;
  
  ENTER();

  if (!IS_ENGINE(engine) || !IS_DICTIONARY(d)) {
    err("LEAVE, args error");
    return eci_res;
  }

  eci_res = DictInternalError;
  msg_set_header(&header, MSG_SET_DICT, engine->handle);
  header.args.sd.hDict = d->handle;

  if (api_lock(engine->api))
    return eci_res;

  if (engine_send_command(engine, &header, NULL, (int*)&eci_res) || eci_res)
    goto exit0;

  d->is_set = true;

  i = d->lang;
  for (j=0; j<NB_DICT_SET; j++) {
    if (engine->dictionary_set[i][j] && (engine->dictionary_set[i][j] != d)) {
      engine->dictionary_set[i][j]->is_set = false;
    }
  }

 exit0:
  api_unlock(engine->api);
  return eci_res;
}

ECIDictHand eciDeleteDict(ECIHand hEngine, ECIDictHand hDict)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct dictionary_t *d = (struct dictionary_t *)hDict;
  int i,j;

  ENTER();

  if (!IS_ENGINE(engine) || !IS_DICTIONARY(d)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_DELETE_DICT, engine->handle);
  header.args.dd.hDict = d->handle;
  if (api_lock(engine->api))
    return eci_res;

  if (engine_send_command(engine, &header, NULL, (int*)&eci_res) || eci_res)
    goto exit0;

  i = d->lang;
  for (j=0; j<NB_DICT_SET; j++) {
    if (engine->dictionary_set[i][j] && (engine->dictionary_set[i][j] == d)) {
      engine->dictionary_set[i][j] = NULL;
      dbg("slot found: engine->dictionary_set[%d][%d]", i, j);
      break;
    }
  }

  d->id = 0;
  for (i=0; i<NB_DICT_VOLUME;i++)
    free(d->filename[i]);
  free(d);

 exit0:
  api_unlock(engine->api);
  return eci_res;
}


enum ECIDictError eciLoadDict(ECIHand hEngine, ECIDictHand hDict,
			      enum ECIDictVolume DictVol, ECIInputText pFilename)
{
  enum ECIDictError eci_res = DictFileNotFound;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct dictionary_t *d = (struct dictionary_t *)hDict;
  int i,j;

  ENTER();

  if (!pFilename) {
    err("LEAVE, args error 0");
    return eci_res;
  }

  if (!IS_ENGINE(engine) || !IS_DICTIONARY(d)) {
    err("LEAVE, args error 1");
    return eci_res;
  }

  msg_set_header(&header, MSG_LOAD_DICT, engine->handle);
  header.args.ld.hDict = d->handle;
  header.args.ld.DictVol = DictVol;

  if (api_lock(engine->api))
    return eci_res;

  if (engine_send_command(engine, &header, pFilename, (int*)&eci_res) || eci_res)
    goto exit0;

  if (DictVol<NB_DICT_VOLUME) {
    free(d->filename[DictVol]);
    d->filename[DictVol] = strdup(pFilename);
  }
    
 exit0:
  api_unlock(engine->api);
  return eci_res;
}


Boolean eciClearInput(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  ENTER();
  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return ECIFalse;
  }
  msg_set_header(&header, MSG_CLEAR_INPUT, engine->handle);
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}


Boolean ECIFNDECLARE eciSetOutputDevice(ECIHand hEngine, int iDevNum)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct api_t *api;
  struct msg_t header;

  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return eci_res;
  }

  msg_set_header(&header, MSG_SET_OUTPUT_DEVICE, engine->handle);
  header.args.sod.iDevNum = iDevNum;
  if (!api_lock(engine->api)) {
    engine_send_command(engine, &header, NULL, &eci_res);
    api_unlock(engine->api);
  }
  return eci_res;
}

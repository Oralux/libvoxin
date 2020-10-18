#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <endian.h>
#include <ctype.h>
#include "voxin.h"
#include "debug.h"
#include "libvoxin.h"
#include "msg.h"
#include "inote.h"
#include "config.h"

#define FILTER_SSML 1
#define FILTER_PUNC 2

#define ENGINE_ID 0x020A0005
#define IS_ENGINE(e) (e && (e->id == ENGINE_ID) && e->handle && e->api && e->current_engine)
#define IS_API(a) (a && (a->my_instance || !(res=api_create(a))))
  
#define MAX_LANG 2 // RFU
#define VOICE_PARAM_UNCHANGED 0x1000

typedef struct {
  int major;
  int minor;
  int patch;
} version_t;

typedef struct {
  version_t msg;
  version_t voxin;
  version_t inote;
  version_t tts;
} voxind_version_t;

struct engine_t {
  uint32_t id; // structure identifier
  struct api_t *api; // parent api
  uint32_t handle;
  struct engine_t *current_engine;
  struct engine_t *other_engine; 
  msg_tts_id tts_id;
  void *cb; // user callback
  void *data_cb; // user data callback
  int16_t *samples; // user samples buffer
  uint32_t nb_samples; // current number of samples in the user sample buffer
  uint32_t stop_required;
  char *output_filename;
  void *inote; // inote handle
  inote_charset_t from_charset; // charset of the text supplied by the caller
  inote_charset_t to_charset; // charset expected by the tts engine
  uint32_t voice_id; // voice identifier, e.g.: 0x2d0002
  uint32_t voice_param[eciNumVoiceParams];
  uint32_t state_expected_lang[MAX_LANG]; // state internal buffer 
  uint8_t tlv_message_buffer[TLV_MESSAGE_LENGTH_MAX]; // tlv internal buffer
  inote_slice_t tlv_message;
  inote_state_t state;  
  uint8_t text_buffer[TEXT_LENGTH_MAX];  
};

#define ALLOCATED_MSG_LENGTH PIPE_MAX_BLOCK

struct api_t {
  void *my_instance; // communication channel with voxind. my_api is fully created when my_instance is non NULL 
  msg_tts_id tts[MSG_TTS_MAX]; // installed tts
  size_t tts_len; // number of elements of the tts array
  pthread_mutex_t stop_mutex; // to process only one stop command
  pthread_mutex_t api_mutex; // to process exclusively any other command
  struct msg_t *msg; // message for voxind
  voxind_version_t voxind_version[MSG_TTS_MAX]; // version of voxind and its components
  config_t *my_config;
};

static struct api_t my_api = {.stop_mutex=PTHREAD_MUTEX_INITIALIZER, .api_mutex=PTHREAD_MUTEX_INITIALIZER, NULL};

static vox_t vox_list[MSG_VOX_LIST_MAX];
static int vox_list_nb;
static int voxDefaultParam[VOX_NUM_PARAMS];

// TODO: in conf
#define SOUNDS_DIR "/opt/oralux/voxin/share/sounds"

#define WAV_HEADER_SIZE 0x2c
#define SOUND_SIZE_MAX 4096
static const char *sound_filename[] = {"capitals.wav", "capital.wav"}; // longest word first (for realloc)
typedef enum {SOUND_CAPITALS, SOUND_CAPITAL, SOUND_MAX} sound_id; // identify sounds_filename

typedef struct {
  uint8_t buf[SOUND_SIZE_MAX];
  size_t len; // length in bytes really used
  size_t cap; // max allocated size
  int freq; // 11025, 22050
} sound_t;

typedef struct {
  sound_t sound[SOUND_MAX][MSG_TTS_MAX];
} sounds_t;

static sounds_t sounds;

static int frequence[MSG_TTS_MAX] = {0, 11025, 22050};

static void conv_int_to_version(int src, version_t *dst) {
  if (dst) {
    *dst = (version_t){
      .major = (src>>16) & 0xff,
      .minor = (src>>8) & 0xff,
      .patch = src & 0xff,
    };
  }
}

static inote_charset_t getCharset(enum ECILanguageDialect lang)
{
  inote_charset_t charset = INOTE_CHARSET_UTF_8; // default for other engine
  if ((lang <= eciStandardItalian)
	  || (lang == eciBrazilianPortuguese)
	  || (lang == eciStandardFinnish)) {
	return INOTE_CHARSET_ISO_8859_1; // 1 byte
  }

  switch(lang) {
  case eciMandarinChinese:
  case eciMandarinChinesePinYin:
  case eciStandardCantonese:
	charset = INOTE_CHARSET_GBK; // 1 or 2 bytes
	break;
  case eciMandarinChineseUCS:
  case eciTaiwaneseMandarinUCS:
  case eciStandardJapaneseUCS:
  case eciStandardCantoneseUCS:
  case eciHongKongCantoneseUCS:
	charset = INOTE_CHARSET_UCS_2; // 2 bytes
	break;
  case eciTaiwaneseMandarin:
  case eciTaiwaneseMandarinZhuYin:
  case eciTaiwaneseMandarinPinYin:
  case eciHongKongCantonese:
	charset = INOTE_CHARSET_BIG_5; // 1 or 2 bytes
	break;
  case eciStandardJapanese:
	charset = INOTE_CHARSET_SJIS; // 1 or 2 bytes
	break;
  default:
	dbg("extended eci value:%x", lang);
	break;
  }
  return charset;
}

static void sound_create() {
  ENTER();
  static bool once = false;
  char *pathname = NULL;
  int i;

  if (once)
    return;

  once = true;
  memset(&sounds, 0, sizeof(sounds));

  for (i=0; i<SOUND_MAX; i++) {
    int j;
    for (j=1; j<MSG_TTS_MAX; j++) {
      sound_t *sound = &sounds.sound[i][j];
      { // get wav pathname
	static char c[40]; // 40 to silent gcc warning      
	size_t size = snprintf(c, sizeof(c), "%s/%d/%s", SOUNDS_DIR, frequence[j], sound_filename[i]);
	if (size<0) {
	  break;
	}
	size++; // terminator
	char *buf = realloc(pathname, size);
	if (!buf) {
	  break;
	}
	pathname = buf;
	size = snprintf(pathname, size, "%s/%d/%s", SOUNDS_DIR, frequence[j], sound_filename[i]);
	if (size<0) {
	  break;
	}
      }
      
      sound->cap = sizeof(sound->buf);
      sound->freq = frequence[j];

      { // read wav
	FILE *fd = fopen(pathname, "r");
	if (fd) {
	  fseek(fd, WAV_HEADER_SIZE, SEEK_SET);
	  sound->len = fread(sound->buf, 1, sound->cap, fd);
	  fclose(fd);
	  dbg("sounds[%d][%d]: len=%lu (%s)", i, j, (unsigned long)sound->len, pathname);
	}
      }
    }
  }

  if (pathname) {
    free(pathname);
    pathname = NULL;
  }  
}

static int api_create(struct api_t *api) {
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
  api->msg = (struct msg_t*)calloc(1, PIPE_MAX_BLOCK);  
  if (!api->msg) {
	res = errno;
	goto exit0;
  }
  
  api->my_instance = libvoxin_create(&api->my_instance);
  res = libvoxin_list_tts(api->my_instance, NULL, &api->tts_len);
  if (!res && api->tts_len) {
	if (api->tts) {
	  res = libvoxin_list_tts(api->my_instance, api->tts, &api->tts_len);
	}
  }

  sound_create();  
  config_create(&api->my_config);
  
 exit0:
  if ((!api->my_instance) && api->msg) {
	free(api->msg);
	api->msg = NULL;
  }
  {
	int res;
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


static int msg_set_header(struct msg_t *msg, uint32_t id, uint32_t func, uint32_t engine_handle)
{
  dbg("ENTER(0x%0x,%d,0x%0x)", id, func, engine_handle);
  if (!msg || !MSG_CHECK(id)) {
	err("LEAVE, args error");
	return EINVAL;
  }

  memset(msg, 0, MSG_HEADER_LENGTH);
  msg->id = id;
  msg->func = func;
  msg->engine = engine_handle;
  return 0;  
}


static int msg_copy_bytes(struct msg_t *msg, const struct msg_bytes_t *bytes)
{
  char *c = NULL;
  int effective_msg_length;
  size_t len;
  
  ENTER();
  if (!msg || !bytes) {
	err("LEAVE, args error");
	return EINVAL;
  }

  len = min_size(bytes->len, ALLOCATED_MSG_LENGTH - MSG_HEADER_LENGTH - 1);
  c = (char*)memcpy(msg->data, bytes->b, len);
  c += len;

  dbg_bytes("bytes=", bytes);

  effective_msg_length = (size_t)((char*)c - (char*)msg);
  msg->effective_data_length = effective_msg_length - MSG_HEADER_LENGTH;
  dbg("data length=%d", msg->effective_data_length);

  LEAVE();
  return 0;
}


static int api_lock(struct api_t *api)
{
  int res = 0;

  ENTER();

  if (!IS_API(api)) {
	err("LEAVE, error %d", res);
	return res;
  }
  
  res = pthread_mutex_lock(&api->api_mutex);
  if (res) {
	err("LEAVE, api_mutex error l (%d)", res);
  }

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

// Notes:
// The caller must lock the mutex if with_lock is set to false. 
// If the returned value is not 0, the mutex is unlocked whichever the value of
// with_unlock.
static int process_func1(struct api_t* api, struct msg_t *header, const struct msg_bytes_t *bytes,
						 int *eci_res, bool with_unlock, bool with_lock)
{
  int res = EINVAL;  
  uint32_t c;
  
  ENTER();

  if (!api || !header) {
	err("LEAVE, args error");
	return res;
  }

  if (with_lock) {
	res = api_lock(api);
	if (res)
	  return res;
	usleep(1000);
  }

  c = api->msg->count;
  memcpy(api->msg, header, sizeof(*api->msg));
  api->msg->count = c;
  
  if (bytes) {
	res = msg_copy_bytes(api->msg, bytes);
	if (res)
	  goto exit0;
  }
  api->msg->allocated_data_length = ALLOCATED_MSG_LENGTH;

  res = libvoxin_call_eci(api->my_instance, api->msg);

 exit0:
  if (res) {
	api_unlock(api);
  } else {
	if (eci_res)
	  *eci_res = api->msg->res;

	if (with_unlock)
	  res = api_unlock(api);
  }
  
  LEAVE();
  return res;
}
  
static void engine_init_buffers(struct engine_t *self) {
  if (self) {
	self->tlv_message.buffer = self->tlv_message_buffer;
	self->tlv_message.end_of_buffer = self->tlv_message_buffer + sizeof(self->tlv_message_buffer);	  
	self->tlv_message.length = 0;
	self->tlv_message.charset = self->to_charset;	
	self->state.expected_lang = self->state_expected_lang;
  }
}

static struct engine_t *engine_create(uint32_t handle, struct api_t *api, msg_tts_id tts_id)
{
  struct engine_t *self = NULL;
  
  ENTER();
 
  self = (struct engine_t*)calloc(1, sizeof(*self));
  if (self) {
	self->id = ENGINE_ID;
	self->handle = handle;
	self->current_engine = self;
	self->api = api;
	self->tts_id = tts_id;
	self->inote = inote_create();
	{
	  version_t *v = &api->voxind_version[tts_id].inote;
	  inote_set_compatibility(self->inote, v->major, v->minor, v->patch);
	}
	int i;
	for (i=0; i<sizeof(self->voice_param)/sizeof(*self->voice_param); i++)
	  self->voice_param[i] = VOICE_PARAM_UNCHANGED;
	engine_init_buffers(self);
	// TODO: state init (expected languages/annotation)
	/* state.expected_lang[0] = ENGLISH; */
	/* state.expected_lang[1] = FRENCH; */
	/* state.max_expected_lang = MAX_LANG; */
	self->state.annotation = 1; // TODO
	// input text: by default ssml/utf-8 is expected 
	self->state.ssml = 1;
	self->from_charset = self->to_charset = INOTE_CHARSET_UTF_8;
  } else {
	err("mem error (%d)", errno);
  }

  LEAVE();
  return self;
}


static struct engine_t *engine_delete(struct engine_t *self)
{
  ENTER();

  if (!self)
	return NULL;

  inote_delete(self->inote);
  engine_delete(self->other_engine);
  if (self->output_filename)
	free(self->output_filename);
  
  memset(self, 0, sizeof(*self));
  free(self);
  self = NULL;

  LEAVE();
  return self;
}

// to be called with a lock on api
static int getCurrentLanguage(struct engine_t *engine)    
{
  int eci_res = 0;
  int res = 0;
  struct msg_t header;

  ENTER();
    
  if (!engine)
	return 0;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = eciLanguageDialect;
  res = process_func1(engine->api, &header, NULL, &eci_res, false, false);
  if (!res) {    
	engine->voice_id = eci_res;
	engine->to_charset = getCharset((enum ECILanguageDialect)engine->voice_id);
  }    
  return res;  
}

static void setPunctuationMode(struct engine_t *engine, inote_punct_mode_t mode, const char *punctuation_list)
{
  const char *fmt = " `Pf%d%s ";
  enum {CHAR_MAX = 20};
  char text[CHAR_MAX];
  const char *list = punctuation_list;
  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return;
  }
  engine = engine->current_engine;

  if (list) {
    int i;
    for (i=0; i<CHAR_MAX && list[i]; i++) {
      if (!ispunct(list[i])) {
	list = NULL;
	break;
      }
    }
  }
  
  if (!list) {
    config_t init = CONFIG_DEFAULT;
    list = init.some_punctuation;
  }
  
  snprintf(text, CHAR_MAX, fmt, mode, list);
  
  text[CHAR_MAX-2] = ' ';
  text[CHAR_MAX-1] = 0;
  eciAddText(engine, text);
}

// set value of parameters according to the configuration file
static void setConfiguredValues(struct engine_t *engine)
{
  struct api_t *api;
  
  ENTER();

  if (!IS_ENGINE(engine)) {
    err("LEAVE, args error");
    return;
  }
  engine = engine->current_engine;

  api = engine->api;

  if (!engine || !api || !api->my_config)
    return;

  {
    config_t *config = api->my_config;
    config_t init = CONFIG_DEFAULT;
    if (config->capital_mode != init.capital_mode) {
      voxSetParam(engine, VOX_CAPITALS, config->capital_mode);
    }
    if (config->punctuation_mode != init.punctuation_mode) {
      setPunctuationMode(engine, config->punctuation_mode, config->some_punctuation);
    }
  }
}

ECIHand eciNew(void)
{
  int eci_res = 0;
  struct engine_t *engine = NULL;
  struct msg_t header;
  struct api_t *api = &my_api;
  int res = 0;
 
  ENTER();

  if (!IS_API(api)) {
	err("LEAVE, error %d", res);
	return NULL;
  }

  if (!vox_list_nb) {
	unsigned int n = MSG_VOX_LIST_MAX;
	if (voxGetVoices(NULL, &n)) {
	  err("LEAVE, error getting voices");    
	  return NULL;
	}	  
  }
  if (!vox_list_nb) {
	err("LEAVE, error no voice");    
	return NULL;
  }
  res = api_lock(api);
  if (res) {
	err("LEAVE, error api lock");    
	return NULL;
  }
  //  usleep(1000);

  // look for the first available tts
  int i;
  msg_tts_id tts_id = MSG_TTS_UNDEFINED;
  for (i=0; i<api->tts_len; i++) {
	if (api->tts[i] != MSG_TTS_UNDEFINED) {	  
	  tts_id = api->tts[i];
	  break;
	}
  }
  if (tts_id == MSG_TTS_UNDEFINED) {
	dbg("no tts available");
	return NULL;	
  }
  if (msg_set_header(&header, MSG_DST(tts_id), MSG_NEW, 0)) {
	err("LEAVE, error set header");        
	return NULL;
  }

  if (!process_func1(api, &header, NULL, &eci_res, false, false)) {
	if (eci_res != 0) {
	  engine = engine_create(eci_res, api, tts_id);
	}
    
	eci_res = getCurrentLanguage(engine);
	if (!eci_res)
	  api_unlock(api);
  }

  setConfiguredValues(engine);
  
  dbg("LEAVE, engine=%p", engine);
  return (ECIHand)engine;
}


Boolean eciSetOutputBuffer(ECIHand hEngine, int iSize, short *psBuffer)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  
  dbg("ENTER(%p,%d,%p)", hEngine, iSize, psBuffer);
 
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;

  if (msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SET_OUTPUT_BUFFER, engine->handle))
	goto exit0;
  
  header.args.sob.nb_samples = iSize;

  if (!process_func1(engine->api, &header, NULL, &eci_res, false, true)) {  
	if (eci_res == ECITrue) {
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
  struct msg_bytes_t bytes;
  bytes.b = (uint8_t*)pFilename;
  bytes.len = strlen((char*)pFilename);

  ENTER();

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECIFalse;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SET_OUTPUT_FILENAME, engine->handle);
  process_func1(engine->api, &header, &bytes, &eci_res, true, true);
  if (eci_res == ECITrue) {
	if (engine->output_filename)
	  free(engine->output_filename);
	engine->output_filename = strdup(pFilename);
  }
  return eci_res;
}

// read bytes up to len or a zero terminator
static size_t readBytes(const uint8_t *bytes, size_t len) {
  const uint8_t *t, *tmax;
  size_t ret = 0;

  if (!bytes || !len)
	return 0;

  t = bytes;
  tmax = t + len;
  for (;(t< tmax) && *t; t++) {}

  ret = t - bytes;
  
  dbg("new buffer(%p, len=%lu)", bytes, (long unsigned int)ret);  
  return ret;
}

// copy src to dst
// dst and dst_buffer: allocated by the caller
static int copySlice(const inote_slice_t *src, inote_slice_t *dst, uint8_t *dst_buffer, size_t len) {
  if (!src || !src->buffer || (src->end_of_buffer < src->buffer)
	  || !dst || !dst_buffer)
	return -1;  
  memcpy(dst, src, sizeof(*dst));
  dst->buffer = dst_buffer;
  dst->length = len;
  dst->end_of_buffer = dst->buffer + len;
  memcpy(dst->buffer, src->buffer, len);  
  return 0;
}

// replay the text to tlv conversion using the first/correct text segment
// if skip_byte != 0 then replace the to-be-skipped byte by the space
// character then replay the text up to the skipped byte (included)
// otherwise replay the text except 'text left'
//
// text_left is updated with the remaining text
static int replayText(struct engine_t *engine, inote_slice_t *text, int skip_byte, size_t *text_left) {
  int ret = 0;
  inote_slice_t slice;
  inote_slice_t *t = NULL;  

  if (!engine
	  || !text || !text->buffer
	  || !text->length || (text->length > TEXT_LENGTH_MAX)
	  || !text_left || !*text_left || (*text_left > text->length)) {	
	err("LEAVE, args error");
	return -1;
  }
  
  if (skip_byte) {
	--*text_left;
	size_t len = text->length - *text_left;
	t = &slice;
	copySlice(text, t, engine->text_buffer, len);
	t->buffer[len-1] = ' ';
	dbg("replay text up to the space char (included) (text_left=%lu, skipped byte=0x%02x)", (long unsigned int)*text_left, text->buffer[len-1]);
  } else {
	t = text;
	t->length = text->length - *text_left;
	dbg("replay text up to %x byte (excluded) (text_left=%lu)", t->buffer[t->length + 1], (long unsigned int)*text_left);
  }
  
  engine_init_buffers(engine);	

  size_t text_left2 = 0;
  inote_error status = inote_convert_text_to_tlv(engine->inote, t, &(engine->state), &(engine->tlv_message), &text_left2);
  if (status) {
	err("replay inote_convert_text_to_tlv: ret=%d", status);
	ret = -1;
  }
  return ret;
}

static int switchLanguage(struct engine_t *engine, inote_slice_t *text, size_t *text_left) {
  int ret = 0;

  if (!engine
	  || !text || !text->buffer
	  || !text->length || (text->length > TEXT_LENGTH_MAX)
	  || !text_left || !*text_left || (*text_left > text->length)) {	
	err("LEAVE, args error");
	return -1;
  }
  
  size_t left = *text_left;
  unsigned char *s = text->buffer + text->length - left;
  int i;
  for (i=0; i<left; i++) {
	if (s[i] == ' ')
	  break;
  }
  if (s[i] == ' ') {
	s[i] = 0;
	dbg("annotation: %s\n", s);
	i++;
	if (i < left) {
	  left -= i;
	  text->buffer = s + i;
	  text->length = left;
	  left = 0;
	  engine_init_buffers(engine);	
	  inote_error status = inote_convert_text_to_tlv(engine->inote, text, &(engine->state), &(engine->tlv_message), &left);
	  if (status) {
		err("replay inote_convert_text_to_tlv: ret=%d", status);
		ret = -1;
	  }
	  
	}	
  }

  return ret;  
}


Boolean eciAddText(ECIHand hEngine, ECIInputText pText)
{
  Boolean eci_res = ECITrue;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  inote_slice_t text;
  struct api_t *api;
  int ret_process1 = 0;
	
  dbg("ENTER (%p,%p)", hEngine, pText);

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECIFalse;
  }
  engine = engine->current_engine;

  api = engine->api;
  if (api_lock(api))
	return ECIFalse;

  if (libvoxinDebugEnabled(LV_DEBUG_LEVEL)) {
	dbgText(pText, strlen(pText));
  }
  
  engine_init_buffers(engine);	

  bool loop = true;
  size_t text_left = 0;
  uint8_t *t0, *t;
  t0 = t = (uint8_t*)pText;

  while(loop) {	
	size_t len = readBytes(t, TEXT_LENGTH_MAX - text_left);
	
	t += len;
	text.length = len + text_left;
	if (!text.length) {
	  break;
	}
	text.buffer = t0;
	text.charset = engine->from_charset;
	text.end_of_buffer = t;
	engine_init_buffers(engine);	
	text_left = 0;

	inote_error ret = inote_convert_text_to_tlv(engine->inote, &text, &(engine->state), &(engine->tlv_message), &text_left);
	
	switch (ret) {
	case INOTE_OK:
	  break;
	case INOTE_INVALID_MULTIBYTE:
	  dbg("inote_convert_text_to_tlv: invalid multibyte");
	  loop = (!replayText(engine, &text, 1, &text_left));
	  break;
	case INOTE_INCOMPLETE_MULTIBYTE:
	  dbg("inote_convert_text_to_tlv: incomplete multibyte");
	  loop = (!replayText(engine, &text, 0, &text_left));
	  break;
	case INOTE_LANGUAGE_SWITCHING:
	  switchLanguage(engine, &text, &text_left);
	default:
	  loop = false;
	  break;
	}
	
	t0 = t - text_left;

	if (loop && engine->tlv_message.length) {
	  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_ADD_TLV, engine->handle);
	  struct msg_bytes_t bytes;
	  bytes.b = engine->tlv_message.buffer;
	  bytes.len = engine->tlv_message.length;
	  ret_process1 = process_func1(engine->api, &header, &bytes, &eci_res, false, false);
	  if (ret_process1) 
		loop = false; 
	}
  }

  // api already unlocked if process_func1 return val != 0
  if (!ret_process1) { 
	api_unlock(api);
  }
  
  engine->tlv_message.length = 0;
  return eci_res;
}


Boolean eciSynthesize(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  dbg("ENTER(%p)", hEngine);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECIFalse;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SYNTHESIZE, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
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
  engine = engine->current_engine;

  api = engine->api;

  res = pthread_mutex_lock(&api->api_mutex);
  if (res) {
	err("LEAVE, api_mutex error l (%d)", res);
	return eci_res;
  }
  
  m = api->msg;
  c = m->count;
  msg_set_header(m, MSG_DST(engine->tts_id), type, engine->handle);
  m->count = c;
  m->allocated_data_length = ALLOCATED_MSG_LENGTH;
  res = libvoxin_call_eci(api->my_instance, m);
  if (res)
	goto exit0;

  while(1) {
	if (m->func < MSG_CB_WAVEFORM_BUFFER)
	  break;

	m->res = eciDataAbort;

	int lParam = -1;
	if (engine->cb && engine->samples
		&& (m->effective_data_length <= 2*engine->nb_samples)) {
	  ECICallback cb = (ECICallback)engine->cb;
	  enum ECIMessage Msg = (enum ECIMessage)(m->func - MSG_CB_WAVEFORM_BUFFER + eciWaveformBuffer);

	  switch(Msg) {
	  case eciWaveformBuffer:
	    	dbg("lParam=0x%08x)", m->args.cb.lParam);	    
		if (m->args.cb.lParam == MSG_PREPEND_CAPITAL) {		  
		    dbg("prepend capital");
		    sound_t *sound = &sounds.sound[SOUND_CAPITAL][engine->tts_id];
		    size_t sound_length = (sound->len <= 2*engine->nb_samples) ?
		      sound->len : 2*engine->nb_samples;
		    lParam = sound_length/2;
		    dbg("len audio[%d]=%d", engine->tts_id, lParam);
		    memcpy(engine->samples, sound->buf, sound_length);
		    m->res = (enum ECICallbackReturn)cb((ECIHand)((char*)NULL+engine->handle),
							Msg, lParam, engine->data_cb);
		} else if (m->args.cb.lParam == MSG_PREPEND_CAPITALS) {
		    dbg("prepend capitals");
		    sound_t *sound = &sounds.sound[SOUND_CAPITALS][engine->tts_id];
		    size_t sound_length = (sound->len <= 2*engine->nb_samples) ?
		      sound->len : 2*engine->nb_samples;
		    lParam = sound_length/2;
		    memcpy(engine->samples, sound->buf, sound_length);
		    m->res = (enum ECICallbackReturn)cb((ECIHand)((char*)NULL+engine->handle), Msg, lParam, engine->data_cb);
		}
		
		lParam = m->effective_data_length/2;
		memcpy(engine->samples, m->data, m->effective_data_length);
		break;
	  case eciPhonemeBuffer:
		lParam = m->effective_data_length;
		memcpy(engine->samples, m->data, m->effective_data_length);
		break;
	  case eciIndexReply:
	  case eciPhonemeIndexReply:
	  case eciWordIndexReply:
	  case eciStringIndexReply:
	  case eciSynthesisBreak:
		lParam = le32toh(m->args.cb.lParam);
		break;
	  default:
		err("unknown eci message (%d)", Msg);
	  }
	  if (lParam != -1) {
		dbg("call user callback, handle=0x%x, msg=%s, lParam=%d",
			engine->handle, msg_string((enum msg_type)(m->func)), lParam);
		m->res = (enum ECICallbackReturn)cb((ECIHand)((char*)NULL+engine->handle), Msg, lParam, engine->data_cb);
	  }
	}
	if(lParam == -1) {
	  err("error callback, handle=0x%x, msg=%s, #samples=%d",
		  engine->handle, msg_string((enum msg_type)(m->func)), m->effective_data_length/2);
	}
	
	dbg("res user callback=%d", m->res);
	if (engine->stop_required) {
	  m->res = eciDataAbort;
	  dbg("stop required");
	}

	m->id = MSG_DST(engine->tts_id);
	m->allocated_data_length = ALLOCATED_MSG_LENGTH;
	res = libvoxin_call_eci(api->my_instance, m);
	if (res)
	  goto exit0;
  }

 exit0:
  if (!res) {
	eci_res =  m->res;
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
  
  dbg("ENTER(%p)", hEngine);

  eci_res = synchronize(engine, MSG_SYNCHRONIZE);
  
  LEAVE();
  return eci_res;
}


ECIHand eciDelete(ECIHand hEngine)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  ECIHand handle = hEngine;
  int eci_res;
  struct api_t *api;
  struct msg_t header;
    
  dbg("ENTER(%p)", hEngine);

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return handle;
  }

  api = engine->api;
  if (api_lock(api))
	return handle;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_DELETE, engine->handle);
  if (!process_func1(engine->api, &header, NULL, (int*)&eci_res, false, false)) {
	if (eci_res == NULL_ECI_HAND) {
	  engine_delete(engine);
	  handle = NULL_ECI_HAND;
	}
	api_unlock(api);
  }
  
  LEAVE();
  return handle;
}


void eciRegisterCallback(ECIHand hEngine, ECICallback Callback, void *pData)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%p,%p)", hEngine, Callback, pData);
  
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_REGISTER_CALLBACK, engine->handle);
  header.args.rc.Callback = !!Callback;

  if (!process_func1(engine->api, &header, NULL, NULL, 0, 1)) {
	engine->cb = (void*)Callback;
	engine->data_cb = pData;
	api_unlock(engine->api);
  }  

  LEAVE();
}


Boolean eciSpeaking(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  
  dbg("ENTER(%p)", hEngine);  

  eci_res = synchronize(engine, MSG_SPEAKING);  

  LEAVE();
  return eci_res;
}


Boolean eciStop(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct api_t *api;
  int res = 0;
  
  ENTER();

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;
    
  api = engine->api;
  if (!IS_API(api)) {
	err("LEAVE, error %d", res);
	return ECIFalse;
  }

  res = pthread_mutex_lock(&api->stop_mutex);
  if (res) {
	err("LEAVE, stop_mutex error l (%d)", res);
	return eci_res;
  }

  engine->stop_required = 1;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_STOP, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);

  engine->stop_required = 0;
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
  int max = 0;
   
  dbg("ENTER(%p,%p)", aLanguages, nLanguages);  

  if (!aLanguages || !nLanguages) {
	err("LEAVE, args error");
	return eci_res;
  }

  max = *nLanguages;
  if (!vox_list_nb) {
	if (voxGetVoices(NULL, (unsigned int*)nLanguages))
	  return ECI_PARAMETERERROR;
  }
  if (!vox_list_nb) {
	return ECI_PARAMETERERROR;
  }
  if (!aLanguages) {
	*nLanguages = vox_list_nb;
	return 0;
  }

  *nLanguages = (vox_list_nb <= max) ? vox_list_nb : max;	
  int i;
  for (i=0; i < *nLanguages; i++) {
	aLanguages[i] = vox_list[i].id;
  }
  
  return 0;
}

ECIHand eciNewEx(enum ECILanguageDialect Value)
{
  int eci_res;
  struct engine_t *engine = NULL;
  struct msg_t header;
  struct api_t *api = &my_api;
  int res = 0;
 
  dbg("ENTER(0x%0x)", Value);

  if (!IS_API(api)) {
	err("LEAVE, error %d", res);
	return NULL;
  }
  
  if (!vox_list_nb) {
	unsigned int n = MSG_VOX_LIST_MAX;
	if (voxGetVoices(NULL, &n)) {
	  err("LEAVE, error getting voices");    
	  return NULL;
	}
  }
  if (!vox_list_nb) {
	err("LEAVE, error no voice");    
	return NULL;
  }
  int i,j;
  for (i=0, j=-1; i < vox_list_nb; i++) {
	if (vox_list[i].id == Value) {
	  j = i;
	  break;
	}
  }
  if (j == -1) {
	err("LEAVE, error voice not found");    
	return NULL;
  }
    
  res = api_lock(api);
  if (res) {
	err("LEAVE, error api lock");    
	return NULL;
  }
  //  usleep(1000);

  if (msg_set_header(&header, MSG_DST(vox_list[j].tts_id), MSG_NEW_EX, 0)) {
	err("LEAVE, error set header");        
	return NULL;
  }

  header.args.ne.Value = Value;

  if (!process_func1(api, &header, NULL, &eci_res, false, false)) {
	if (eci_res != 0) {
	  engine = engine_create(eci_res, api, vox_list[j].tts_id);
	  engine->to_charset = getCharset(Value);
	  engine->voice_id = Value;
	}
	api_unlock(api);
  }

  setConfiguredValues(engine);
  
  dbg("LEAVE, engine=%p", engine);
  return (ECIHand)engine;
}

static bool ttsIsIdCompatible(uint32_t id, msg_tts_id tts_id) {
  bool res=false;
  res = (((tts_id == MSG_TTS_ECI) && (id <= VOX_LAST_ECI_VOICE))
		 || ((tts_id == MSG_TTS_NVE) && (id > VOX_LAST_ECI_VOICE)));
  dbg("ENTER(id=0x%0x, tts_id=%d): %d", id, tts_id, res);
  return res;
}

static int engine_copy(struct engine_t *src, struct engine_t *dst) {
  int ret = 0;

  if (!src || !dst) {	
	err("LEAVE, args error");
	return -1;
  }

  if (src->output_filename &&
	  (!dst->output_filename || strcmp(src->output_filename, dst->output_filename))) {
	if (ECIFalse == eciSetOutputFilename(dst, src->output_filename))
	  return -1;
  }

  if (src->samples) {
	if ((src->samples != dst->samples) || (src->nb_samples != dst->nb_samples)){
	  if (ECIFalse == eciSetOutputBuffer(dst, src->nb_samples, src->samples))
		return -1;
	}
  }

  if ((src->cb != dst->cb) || (src->data_cb != dst->data_cb)){
	eciRegisterCallback(dst, src->cb, src->data_cb);
  }

  int i;
  for (i=0; i<sizeof(src->voice_param)/sizeof(*src->voice_param); i++) {
	dbg("get engine=%p, voice_param[%d] = %d)", src, i, src->voice_param[i]);  
	//	if ((src->voice_param[i] != VOICE_PARAM_UNCHANGED) && (src->voice_param[i] != dst->voice_param[i])) {
	if (src->voice_param[i] != VOICE_PARAM_UNCHANGED) {
	  eciSetVoiceParam(dst, 0, i, src->voice_param[i]);
	}
  }

  
  
  /* // update other_engine from self */
  /* self->current_engine = self->other_engine; */
  /* engine_copy(self, self->other_engine); */

  return ret;
}

int set_param(ECIHand hEngine, uint32_t msg_id, voxParam Param, int iValue)
{
  int eci_res = -1;
  struct engine_t *self = (struct engine_t *)hEngine;
  struct engine_t *engine = self;
  struct msg_t header;
   
  dbg("ENTER(%d, 0x%0x)", Param, iValue);  

  if (!IS_ENGINE(engine) || (Param < 0) || (Param >= VOX_NUM_PARAMS)) {
	err("LEAVE, args error");
	return eci_res;
  }

  if (Param == VOX_LANGUAGE_DIALECT) {
	if (!ttsIsIdCompatible(iValue, self->current_engine->tts_id)) {
	  if (self->current_engine == self) {	  
		if (!self->other_engine) {
		  self->other_engine = eciNewEx(iValue);
		}
		if (!self->other_engine)
		  return -1;
		dbg("update other_engine from self");
		self->current_engine = self->other_engine;
		engine_copy(self, self->other_engine);
	  } else {
		dbg("update self from other_engine");
		self->current_engine = self;
		engine_copy(self->other_engine, self);
	  }
	}
  }

  engine = self->current_engine;
  
  msg_set_header(&header, MSG_DST(engine->tts_id), msg_id, engine->handle);
  header.args.sp.Param = Param;
  header.args.sp.iValue = iValue;
  if (!process_func1(engine->api, &header, NULL, &eci_res, false, true)) {
	if (Param == VOX_LANGUAGE_DIALECT) {
	  engine->voice_id = iValue;	  
	  engine->to_charset = getCharset((enum ECILanguageDialect)engine->voice_id);
	}
	api_unlock(engine->api);	      
  }

  return eci_res;
}

int eciSetParam(ECIHand hEngine, enum ECIParam Param, int iValue)
{
    dbg("ENTER(%p, 0x%0x, 0x%0x)", hEngine, Param, iValue);
    return set_param(hEngine, MSG_SET_PARAM, Param, iValue);
}

int voxSetParam(void *handle, voxParam param, int value)
{
    dbg("ENTER(%p, 0x%0x, 0x%0x)", handle, param, value);
    return set_param(handle, MSG_VOX_SET_PARAM, param, value);
}

int eciGetParam(ECIHand hEngine, enum ECIParam Param)
{
  int eci_res = VOX_PARAM_OUT_OF_RANGE;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%d)", Param);  

  if (!IS_ENGINE(engine) || (Param < 0) || (Param >= eciNumParams)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = Param;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;  
}

// TODO eciSetDefaultParam
int eciSetDefaultParam(enum ECIParam parameter, int value)
{
  ENTER();
  int res = -1;
  if ((parameter >= 0) && (parameter < eciNumParams)) {
	res = voxDefaultParam[parameter];
	voxDefaultParam[parameter] = value;
  }  
  return res;
}

// TODO eciGetDefaultParam
int eciGetDefaultParam(enum ECIParam parameter)
{
  int res = ((parameter >= 0) && (parameter < eciNumParams)) ? voxDefaultParam[parameter] : -1; 
  dbg("res = %d", res);
  return res;
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
	err("LEAVE, args error");
	return;
  }
  engine = engine->current_engine;

  api = engine->api;
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_ERROR_MESSAGE, engine->handle);
  if (!process_func1(engine->api, &header, NULL, NULL, false, true)) {
	memccpy(buffer, api->msg->data, 0, MSG_ERROR_MESSAGE);
	msg("msg=%s", (char*)buffer);
	api_unlock(engine->api);	
  }
  
  LEAVE();
}


int eciProgStatus(ECIHand hEngine)
{
  int eci_res = ECI_SYSTEMERROR;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  dbg("ENTER(%p)", hEngine);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECI_PARAMETERERROR;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_PROG_STATUS, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}
  

void eciClearErrors(ECIHand hEngine)
{
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  dbg("ENTER(%p)", hEngine);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_CLEAR_ERRORS, engine->handle);
  process_func1(engine->api, &header, NULL, NULL, true, true);  
}

Boolean eciReset(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  dbg("ENTER(%p)", hEngine);
	
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECIFalse;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_RESET, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}


void eciVersion(char *pBuffer)
{
  ENTER();

  if (!pBuffer) {
	err("LEAVE, args error 0");     
	return;
  }

  snprintf(pBuffer, 20, "%d.%d.%d", LIBVOXIN_VERSION_MAJOR, LIBVOXIN_VERSION_MINOR, LIBVOXIN_VERSION_PATCH);
  pBuffer[19]=0;
  msg("version=%s", (char*)pBuffer);
  
  LEAVE();
}


int eciGetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%d,%d)", hEngine, iVoice, Param);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;
    
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_GET_VOICE_PARAM, engine->handle);
  header.args.gvp.iVoice = iVoice;
  header.args.gvp.Param = Param;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}


int eciSetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param, int iValue)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%d,%d,%d)", hEngine, iVoice, Param, iValue);  
  
  if (!IS_ENGINE(engine) || (Param >= eciNumVoiceParams)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SET_VOICE_PARAM, engine->handle);
  header.args.svp.iVoice = iVoice;
  header.args.svp.Param = Param;
  header.args.svp.iValue = iValue;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  if (eci_res >= 0) {
	engine->voice_param[Param] = iValue;
	dbg("set engine=%p, voice_param[%d] = %d)", engine, Param, iValue);  
  }
  return eci_res;
}


Boolean eciPause(ECIHand hEngine, Boolean On)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  
  dbg("ENTER(%p,%d)", hEngine, On);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_PAUSE, engine->handle);
  header.args.p.On = On;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;  
}


Boolean eciInsertIndex(ECIHand hEngine, int iIndex)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%d)", hEngine, iIndex);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;
    
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_INSERT_INDEX, engine->handle);
  header.args.ii.iIndex = iIndex;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;  
}

Boolean eciCopyVoice(ECIHand hEngine, int iVoiceFrom, int iVoiceTo)
{
  int eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  
  dbg("ENTER(%p,%d,%d)", hEngine, iVoiceFrom, iVoiceTo);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;
    
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_COPY_VOICE, engine->handle);
  header.args.cv.iVoiceFrom = iVoiceFrom;
  header.args.cv.iVoiceTo = iVoiceTo;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);

  return eci_res;  
}


ECIDictHand eciNewDict(ECIHand hEngine)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  dbg("ENTER(%p)", hEngine);
	
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return NULL_DICT_HAND;
  }
  engine = engine->current_engine;
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_NEW_DICT, engine->handle);
  process_func1(engine->api, &header, NULL, (int*)&eci_res, true, true);
  return eci_res;
}

ECIDictHand eciGetDict(ECIHand hEngine)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;

  dbg("ENTER(%p)", hEngine);
	
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return NULL_DICT_HAND;
  }
  engine = engine->current_engine;
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_GET_DICT, engine->handle);
  process_func1(engine->api, &header, NULL, (int*)&eci_res, true, true);
  return eci_res;
}

enum ECIDictError eciSetDict(ECIHand hEngine, ECIDictHand hDict)
{
  enum ECIDictError eci_res = DictInternalError;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%p)", hEngine, hDict);

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SET_DICT, engine->handle);
  header.args.sd.hDict = (char*)hDict - (char*)NULL;
  process_func1(engine->api, &header, NULL, (int*)&eci_res, true, true);
  return eci_res;  
}  

ECIDictHand eciDeleteDict(ECIHand hEngine, ECIDictHand hDict)
{
  ECIDictHand eci_res = NULL_DICT_HAND;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  ENTER();

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;
    
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_DELETE_DICT, engine->handle);
  header.args.dd.hDict = (char*)hDict - (char*)NULL;
  process_func1(engine->api, &header, NULL, (int*)&eci_res, true, true);
  return eci_res;  
}


enum ECIDictError eciLoadDict(ECIHand hEngine, ECIDictHand hDict, enum ECIDictVolume DictVol, ECIInputText pFilename)
{
  enum ECIDictError eci_res = DictFileNotFound;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  struct msg_bytes_t bytes;
	
  dbg("ENTER(%p)", hEngine);

  if (!pFilename) {
	err("LEAVE, args error 0");     
	return eci_res;
  }

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error 1");
	return eci_res;
  }
  engine = engine->current_engine;
  
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_LOAD_DICT, engine->handle);
  header.args.ld.hDict = (char*)hDict - (char*)NULL;
  header.args.ld.DictVol = DictVol;

  bytes.b = (uint8_t*)pFilename;
  bytes.len = strlen((char*)pFilename);
  process_func1(engine->api, &header, &bytes, (int*)&eci_res, true, true);
  return eci_res;
}


Boolean eciClearInput(ECIHand hEngine)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
  dbg("ENTER(%p)", hEngine);

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return ECIFalse;
  }
  engine = engine->current_engine;
  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_CLEAR_INPUT, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}


Boolean ECIFNDECLARE eciSetOutputDevice(ECIHand hEngine, int iDevNum)
{
  Boolean eci_res = ECIFalse;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%p,%d)", hEngine, iDevNum);
	
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
  engine = engine->current_engine;

  msg_set_header(&header, MSG_DST(engine->tts_id), MSG_SET_OUTPUT_DEVICE, engine->handle);
  header.args.sod.iDevNum = iDevNum;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}

static int ttsGetVoices(msg_tts_id id, vox_t *list, unsigned int *nbVoices) {
  struct msg_t header;
  struct api_t *api = &my_api;
  int eci_res = 1;
  unsigned int max = 0;
  
  if ((id <= MSG_TTS_UNDEFINED) || (id >= MSG_TTS_MAX) || !list || !nbVoices) {
	err("LEAVE, args error");
	return 1;
  }

  max = *nbVoices;
  *nbVoices = 0;
  dbg("ENTER(%p,max=%d)", list, max);

  msg_set_header(&header, MSG_DST(id), MSG_VOX_GET_VOICES, 0);
  if (process_func1(api, &header, NULL, &eci_res, false, false) || eci_res)
	return 1;
  
  struct msg_vox_get_voices_t *data = (struct msg_vox_get_voices_t *)api->msg->data;
  msg("nb voices=%d", data->nb);
  if (data->nb <= max) {
	max = data->nb;
  }
  int i;
  for (i=0; i<max; i++) {
	list[i].id = data->voices[i].id;
		
	strncpy(list[i].name, data->voices[i].name, MSG_VOX_STR_MAX);
	list[i].lang[MSG_VOX_STR_MAX-1] = 0;
		
	strncpy(list[i].lang, data->voices[i].lang, MSG_VOX_STR_MAX);
	list[i].lang[MSG_VOX_STR_MAX-1] = 0;
		
	strncpy(list[i].variant, data->voices[i].variant, MSG_VOX_STR_MAX);
	list[i].variant[MSG_VOX_STR_MAX-1] = 0;
		  
	list[i].rate = data->voices[i].rate;
	list[i].size = data->voices[i].size;
	strncpy(list[i].charset, data->voices[i].charset, MSG_VOX_STR_MAX);
	list[i].charset[MSG_VOX_STR_MAX-1] = 0;
		  
	list[i].gender = data->voices[i].gender;
	list[i].age = data->voices[i].age;
	strncpy(list[i].multilang, data->voices[i].multilang, MSG_VOX_STR_MAX);
	list[i].multilang[MSG_VOX_STR_MAX-1] = 0;
		
	strncpy(list[i].quality, data->voices[i].quality, MSG_VOX_STR_MAX);
	list[i].quality[MSG_VOX_STR_MAX-1] = 0;
		  
	list[i].tts_id = data->voices[i].tts_id;

	//		  msg("data[%d]=id=0x%x, name=%s, lang=%s, variant=%s, charset=%s", i, data->voices[i].id, data->voices[i].name, data->voices[i].lang, data->voices[i].variant, data->voices[i].charset);
	dbg("vox[%d]=id=0x%x, name=%s, lang=%s, variant=%s, charset=%s", i, list[i].id, list[i].name, list[i].lang, list[i].variant, list[i].charset);
  }
  *nbVoices = max;
  
  return 0;
}

static int ttsGetVersion(msg_tts_id id) {
  ENTER();
  struct msg_t header;
  struct api_t *api = &my_api;
  int eci_res = 1;
	
  if ((id <= MSG_TTS_UNDEFINED) || (id >= MSG_TTS_MAX)) {
    err("LEAVE, args error");
    return 1;
  }
  
  msg_set_header(&header, MSG_DST(id), MSG_GET_VERSIONS, 0);
  if (process_func1(api, &header, NULL, &eci_res, false, false)
      || eci_res) {
    err("LEAVE, response error");
    return 1;
  }
  
  if (!api || !api->msg || !api->msg->data) {
    err("LEAVE, unexpected error");
    return 1;
  }
  
  {
    struct msg_get_versions_t *data = (struct msg_get_versions_t *)api->msg->data;
    voxind_version_t *v = &api->voxind_version[id];
    if ((api->msg->effective_data_length >= sizeof(*data))
	&& (data->magic == MSG_GET_VERSIONS_MAGIC)) {
      conv_int_to_version(data->msg, &v->msg);
      conv_int_to_version(data->voxin, &v->voxin);
      conv_int_to_version(data->inote, &v->inote);
      conv_int_to_version(data->tts, &v->tts);
    } else {
      memset(v, 0, sizeof(*v));
    }
    dbg("versions: msg=%08x, voxin=%08x, inote=%08x, tts=%08x",
	data->msg, data->voxin, data->inote, data->tts);
  }
  return 0;
}

int voxGetVoices(vox_t *list, unsigned int *nbVoices) {
  struct api_t *api = &my_api;
  int res = 0;
  
  if (!nbVoices) {
	err("LEAVE, args error");
	return 1;
  }

  dbg("ENTER(list=%p, nbVoices=%d)", list, *nbVoices);

  if (!IS_API(api)) {
	err("LEAVE, error %d", res);
	return 1;
  }
  
  if (!vox_list_nb) {      
	if (api_lock(api))
	  return 1;

	dbg("voxin api=%d.%d.%d", LIBVOXIN_VERSION_MAJOR, LIBVOXIN_VERSION_MINOR, LIBVOXIN_VERSION_PATCH);

	int i;
	for (i=0; i<api->tts_len; i++) {
	  unsigned int n = MSG_VOX_LIST_MAX - vox_list_nb;
	  int err = ttsGetVoices(api->tts[i], vox_list+vox_list_nb, &n);
	  if (err) {
		api->tts[i] = MSG_TTS_UNDEFINED;
	  } else {		
		vox_list_nb += n;
	  }

	  ttsGetVersion(api->tts[i]);
	}
	api_unlock(api);	
  }

  if (!list) {
	*nbVoices = vox_list_nb;
  } else {
	if (*nbVoices > vox_list_nb) {
	  *nbVoices = vox_list_nb;
	}	
	memcpy(list, vox_list, *nbVoices * sizeof(*list));
  }

  dbg("%d voices", *nbVoices);
  
  return 0;
}

int voxToString(vox_t *data, char *string, size_t *size) {
  //  ENTER();
  char c;
  char *str0;

  if (!data || !size)
    return 1;
  
  if (string) {
    str0 = string;
  } else {
    str0 = &c;
    *size = 1;
  }
  size_t size0 = snprintf(str0, *size, "0x%0x, n=%s, l=%s, v=%s, r=%d, s=%d, c=%s, g=%s, a=%s, q=%s",
			  data->id, data->name, data->lang, data->variant,
			  data->rate, data->size, data->charset,
			  (data->gender == voxFemale) ? "Female" : "Male",
			  (data->age == voxAdult) ? "Adult" : "???",
			  data->quality ? data->quality : "null");
  *size = size0 + 1;
  return 0;
}

int voxGetVersion(int *major, int *minor, int *patch) {
//    ENTER();
  if (!major || !minor || !patch)
	return 1;

    *major = LIBVOXIN_VERSION_MAJOR;
    *minor = LIBVOXIN_VERSION_MINOR;
    *patch = LIBVOXIN_VERSION_PATCH;

    dbg("voxin api=%d.%d.%d", LIBVOXIN_VERSION_MAJOR, LIBVOXIN_VERSION_MINOR, LIBVOXIN_VERSION_PATCH);
    
    return 0;
}

// deprecated
int voxString(vox_t *v, char *s, size_t len) {
  //  ENTER();
    size_t size = len;
    return voxToString(v, s, &size);
}

/* local variables: */
/* c-file-style: "gnu" */
/* end: */

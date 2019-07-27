#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <endian.h>
#include "voxin.h"
#include "debug.h"
#include "libvoxin.h"
#include "msg.h"
#include "inote.h"

#define FILTER_SSML 1
#define FILTER_PUNC 2

#define ENGINE_ID 0x020A0005
#define IS_ENGINE(e) (e && (e->id == ENGINE_ID) && e->handle && e->api)

  
#define MAX_LANG 2 // RFU
  
struct engine_t {
  uint32_t id; // structure identifier
  struct api_t *api; // parent api
  uint32_t handle; // eci handle
  void *cb; // user callback
  void *data_cb; // user data callback
  int16_t *samples; // user samples buffer
  uint32_t nb_samples; // current number of samples in the user sample buffer
  uint32_t stop_required;
  void *inote; // inote handle
  inote_charset_t from_charset; // charset of the text supplied by the caller
  inote_charset_t to_charset; // charset expected by the tts engine
  uint32_t state_expected_lang[MAX_LANG]; // state internal buffer 
  uint8_t tlv_message_buffer[TLV_MESSAGE_LENGTH_MAX]; // tlv internal buffer
  inote_slice_t tlv_message;
  inote_state_t state;
  uint8_t text_buffer[TEXT_LENGTH_MAX];  
};

#define ALLOCATED_MSG_LENGTH PIPE_MAX_BLOCK

struct api_t {
  libvoxin_handle_t my_instance; // communication channel with voxind. my_api is fully created when my_instance is non NULL 
  pthread_mutex_t stop_mutex; // to process only one stop command
  pthread_mutex_t api_mutex; // to process exclusively any other command
  struct msg_t *msg; // message for voxind
};

static struct api_t my_api = {0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, NULL};

static vox_t vox_list[MSG_VOX_LIST_MAX];
static int vox_list_nb;

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
	dbg("unexpected eci value:%x", lang);
	break;
  }
  return charset;
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

  res = libvoxin_create(&api->my_instance, 1); // with_eci = 0 (TODO)

 exit0:
  if (res) {
	err("error (%d)", res);
	if (api->msg)
	  free(api->msg);	
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


static int msg_set_header(struct msg_t *msg, uint32_t func, uint32_t engine_handle)
{
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
  
static struct engine_t *engine_create(uint32_t handle, struct api_t *api)
{
  struct engine_t *self = NULL;
  
  ENTER();

  self = (struct engine_t*)calloc(1, sizeof(*self));
  if (self) {
	self->id = ENGINE_ID;
	self->handle = handle;
	self->api = api;
	self->inote = inote_create();
	engine_init_buffers(self);
	// TODO: state init (expectetd languages/annotation)
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


static struct engine_t *engine_delete(struct engine_t *engine)
{
  ENTER();

  if (!engine)
	return NULL;

  inote_delete(engine->inote);
  
  memset(engine, 0, sizeof(*engine));
  free(engine);
  engine = NULL;

  LEAVE();
  return engine;
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

  msg_set_header(&header, MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = eciLanguageDialect;
  res = process_func1(engine->api, &header, NULL, &eci_res, false, false);
  if (!res) {
	engine->to_charset = getCharset((enum ECILanguageDialect)eci_res);
  }    
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

  if (!process_func1(api, &header, NULL, &eci_res, false, true)) {
	if (eci_res != 0) {
	  engine = engine_create(eci_res, api);
	}
    
	eci_res = getCurrentLanguage(engine);
	if (!eci_res)
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
  
  dbg("ENTER(%p,%d,%p)", hEngine, iSize, psBuffer);
 
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }

  if (msg_set_header(&header, MSG_SET_OUTPUT_BUFFER, engine->handle))
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

  msg_set_header(&header, MSG_SET_OUTPUT_FILENAME, engine->handle);
  process_func1(engine->api, &header, &bytes, &eci_res, true, true);
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
	dbg("replay text up to %x byte (excluded) (text_left=%lu)", t->buffer[t->length + 1], *text_left);
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
	default:
	  loop = false;
	  break;
	}
	
	t0 = t - text_left;

	if (loop && engine->tlv_message.length) {
	  msg_set_header(&header, MSG_ADD_TLV, engine->handle);
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

  msg_set_header(&header, MSG_SYNTHESIZE, engine->handle);
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

	m->id = MSG_TO_ECI_ID;
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

  msg_set_header(&header, MSG_DELETE, engine->handle);
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

  msg_set_header(&header, MSG_REGISTER_CALLBACK, engine->handle);
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
  int res;
  
  ENTER();

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }
    
  api = engine->api;
  if (!api->my_instance) {
	err("LEAVE, args error");
	return eci_res;
  }

  res = pthread_mutex_lock(&api->stop_mutex);
  if (res) {
	err("LEAVE, stop_mutex error l (%d)", res);
	return eci_res;
  }

  engine->stop_required = 1;

  msg_set_header(&header, MSG_STOP, engine->handle);
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
  struct msg_t header;
  struct api_t *api = &my_api;
   
  dbg("ENTER(%p,%p)", aLanguages, nLanguages);  

  if (!aLanguages || !nLanguages) {
	err("LEAVE, args error");
	return eci_res;
  }

  msg_set_header(&header, MSG_GET_AVAILABLE_LANGUAGES, 0);
  if (!process_func1(api, &header, NULL, &eci_res, false, true)) {
	struct msg_get_available_languages_t *lang = (struct msg_get_available_languages_t *)api->msg->data;
	msg("nb lang=%d", lang->nb);
	if (lang->nb <= MSG_LANG_INFO_MAX) {
	  int i;
	  eci_res =  0;
	  *nLanguages = lang->nb;
	  for (i=0; i<lang->nb; i++) {
		aLanguages[i] = (enum ECILanguageDialect)(lang->languages[i]);
		msg("lang[%d]=0x%x", i, aLanguages[i]);
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
 
  dbg("ENTER(%d)", Value);
	
  if (msg_set_header(&header, MSG_NEW_EX, 0))
	return NULL;

  header.args.ne.Value = Value;

  if (!process_func1(api, &header, NULL, &eci_res, false, true)) {
	if (eci_res != 0) {
	  engine = engine_create(eci_res, api);
	  engine->to_charset = getCharset(Value);
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
  struct msg_t header;
   
  dbg("ENTER(%d,0x%0x)", Param, iValue);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }

  msg_set_header(&header, MSG_SET_PARAM, engine->handle);
  header.args.sp.Param = Param;
  header.args.sp.iValue = iValue;
  if (!process_func1(engine->api, &header, NULL, &eci_res, false, true)) {
	if (Param == eciLanguageDialect) {
	  engine->to_charset = getCharset((enum ECILanguageDialect)iValue);
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
   
  dbg("ENTER(%d,%d)", parameter, value);  

  msg_set_header(&header, MSG_SET_DEFAULT_PARAM, 0);
  header.args.sp.Param = parameter;
  header.args.sp.iValue = value;
  process_func1(api, &header, NULL, &eci_res, true, true);
  return eci_res;
}

int eciGetParam(ECIHand hEngine, enum ECIParam Param)
{
  int eci_res = -1;
  struct engine_t *engine = (struct engine_t *)hEngine;
  struct msg_t header;
   
  dbg("ENTER(%d)", Param);  

  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }

  msg_set_header(&header, MSG_GET_PARAM, engine->handle);
  header.args.gp.Param = Param;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;  
}

int eciGetDefaultParam(enum ECIParam parameter)
{
  int eci_res = -1;
  struct msg_t header;
  struct api_t *api = &my_api;
   
  dbg("ENTER(%d)", parameter);  

  msg_set_header(&header, MSG_GET_DEFAULT_PARAM, 0);
  header.args.gp.Param = parameter;
  process_func1(api, &header, NULL, &eci_res, true, true);

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
	err("LEAVE, args error");
	return;
  }

  api = engine->api;
  msg_set_header(&header, MSG_ERROR_MESSAGE, engine->handle);
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

  msg_set_header(&header, MSG_PROG_STATUS, engine->handle);
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
  msg_set_header(&header, MSG_CLEAR_ERRORS, engine->handle);
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
  msg_set_header(&header, MSG_RESET, engine->handle);
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}


void eciVersion(char *pBuffer)
{
  struct api_t *api = &my_api;
  struct msg_t header;

  ENTER();

  if (!pBuffer) {
	err("LEAVE, args error 0");     
	return;
  }

  *(char*)pBuffer=0;

  msg_set_header(&header, MSG_VERSION, 0);
  if (!process_func1(api, &header, NULL, NULL, false, true)) {
	memccpy(pBuffer, api->msg->data, 0, MAX_VERSION);
	msg("version=%s", (char*)pBuffer);
	api_unlock(api);	
  }
  
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
    
  msg_set_header(&header, MSG_GET_VOICE_PARAM, engine->handle);
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
  
  if (!IS_ENGINE(engine)) {
	err("LEAVE, args error");
	return eci_res;
  }

  msg_set_header(&header, MSG_SET_VOICE_PARAM, engine->handle);
  header.args.svp.iVoice = iVoice;
  header.args.svp.Param = Param;
  header.args.svp.iValue = iValue;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
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

  msg_set_header(&header, MSG_PAUSE, engine->handle);
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
    
  msg_set_header(&header, MSG_INSERT_INDEX, engine->handle);
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
    
  msg_set_header(&header, MSG_COPY_VOICE, engine->handle);
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
  msg_set_header(&header, MSG_NEW_DICT, engine->handle);
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
  msg_set_header(&header, MSG_GET_DICT, engine->handle);
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

  msg_set_header(&header, MSG_SET_DICT, engine->handle);
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
    
  msg_set_header(&header, MSG_DELETE_DICT, engine->handle);
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
  
  msg_set_header(&header, MSG_LOAD_DICT, engine->handle);
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
  msg_set_header(&header, MSG_CLEAR_INPUT, engine->handle);
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

  msg_set_header(&header, MSG_SET_OUTPUT_DEVICE, engine->handle);
  header.args.sod.iDevNum = iDevNum;
  process_func1(engine->api, &header, NULL, &eci_res, true, true);
  return eci_res;
}

int voxGetVoices(vox_t *list, unsigned int *nbVoices) {
  struct msg_t header;
  struct api_t *api = &my_api;
  int res = 1;
  
  if (!nbVoices) {
	err("LEAVE, args error");
	return 1;
  }

  dbg("ENTER(%p,%d)", list, *nbVoices);
  
  if (!vox_list_nb) {
	msg_set_header(&header, MSG_VOX_GET_VOICES, 0);
	if (!process_func1(api, &header, NULL, &res, false, true)) {
	  struct msg_vox_get_voices_t *data = (struct msg_vox_get_voices_t *)api->msg->data;
	  msg("nb voices=%d", data->nb);
	  if (data->nb <= MSG_VOX_LIST_MAX) {
		int i;
		res =  0;
		vox_list_nb = data->nb;
		for (i=0; i<data->nb; i++) {
		  vox_list[i].id = data->voices[i].id;

		  strncpy(vox_list[i].name, data->voices[i].name, MSG_VOX_STR_MAX);
		  vox_list[i].lang[MSG_VOX_STR_MAX-1] = 0;

		  strncpy(vox_list[i].lang, data->voices[i].lang, MSG_VOX_STR_MAX);
		  vox_list[i].lang[MSG_VOX_STR_MAX-1] = 0;
		  
		  strncpy(vox_list[i].variant, data->voices[i].variant, MSG_VOX_STR_MAX);
		  vox_list[i].variant[MSG_VOX_STR_MAX-1] = 0;
		  
		  vox_list[i].rate = data->voices[i].rate;
		  vox_list[i].size = data->voices[i].size;
		  strncpy(vox_list[i].charset, data->voices[i].charset, MSG_VOX_STR_MAX);
		  vox_list[i].charset[MSG_VOX_STR_MAX-1] = 0;
		  
		  vox_list[i].gender = data->voices[i].gender;
		  vox_list[i].age = data->voices[i].age;
		  strncpy(vox_list[i].multilang, data->voices[i].multilang, MSG_VOX_STR_MAX);
		  vox_list[i].multilang[MSG_VOX_STR_MAX-1] = 0;
		  
		  strncpy(vox_list[i].quality, data->voices[i].quality, MSG_VOX_STR_MAX);
		  vox_list[i].quality[MSG_VOX_STR_MAX-1] = 0;
		  
		  //		  msg("data[%d]=id=0x%x, name=%s, lang=%s, variant=%s, charset=%s", i, data->voices[i].id, data->voices[i].name, data->voices[i].lang, data->voices[i].variant, data->voices[i].charset);
		  dbg("vox[%d]=id=0x%x, name=%s, lang=%s, variant=%s, charset=%s", i, vox_list[i].id, vox_list[i].name, vox_list[i].lang, vox_list[i].variant, vox_list[i].charset);
		}
	  }	
	  api_unlock(api);	
	}
  }

  if (*nbVoices > vox_list_nb) {
	*nbVoices = vox_list_nb;
  }
  
  if (list) {
	memcpy(list, vox_list, *nbVoices * sizeof(*list));
	return 0;
  }

  return res;
}

int voxString(vox_t *v, char *s, size_t len) {
  //  ENTER();
  snprintf(s, len, "0x%0x, n=%s, l=%s, v=%s, r=%d, s=%d, c=%s, g=%s, a=%s, q=%s",
		   v->id, v->name, v->lang, v->variant,
		   v->rate, v->size, v->charset,
		   (v->gender==voxFemale) ? "Female" : "Male",
		   (v->age == voxAdult) ? "Adult" : "???",
		   v->quality ? v->quality : "null");
  s[len-1] = 0;
  return 0;
}

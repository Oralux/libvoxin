#define _GNU_SOURCE
#include <endian.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"
#include "inote.h"
#include "msg.h"
#include "pipe.h"
#include "voxin.h"

#define VOXIND_ID 0x05000A01 
#define ENGINE_ID 0x15000A01 
#define READ_TIMEOUT_IN_MS 0
#define INDEX_CAPITAL  MSG_PREPEND_CAPITAL
#define INDEX_CAPITALS MSG_PREPEND_CAPITALS
struct voxind_t {
  uint32_t id;
  struct pipe_t *pipe_command;
  struct msg_t *msg;
  size_t msg_length;
};

static struct voxind_t *my_voxind = NULL;

struct engine_t {
  uint32_t id;
  ECIHand handle;
  struct msg_t *cb_msg;
  size_t cb_msg_length;
  inote_charset_t charset; // current charset
  inote_cb_t cb;
  uint8_t tlv_message_buffer[TLV_MESSAGE_LENGTH_MAX]; // tlv internal buffer
  inote_slice_t tlv_message;

  // capital_mode
  // Set by voxSetParam(VOX_CAPITALS, value)
  voxCapitalMode capital_mode;

  // tlv_number:
  // Identify each tlv, incremented for each tlv received.
  // Set to 0 at init or after the completion of eciSynchronize.
  int tlv_number;

  // first_tlv_type:
  // stores the type of the first tlv (tlv_number == 0)
  inote_type_t first_tlv_type;
  
  // audio_sample_received:
  // Set to TRUE when an audio sample is received from the engine.
  // Set to FALSE at init or after the completion of eciSynchronize.
  int audio_sample_received;

};

#define ENGINE_INDEX 0xEA61AE00 
#define ENGINE_MAX_NB VOX_ECI_VOICES
static struct engine_t *engines[ENGINE_MAX_NB]; // array of created engines (not contiguous due to potential deletes)
static size_t engine_number; // number of created engines (and not yet deleted)

// eciLocale, eciLocales from speech-dispatcher (ibmtts.c)
typedef struct _eciLocale {
  char *name;
  char *lang;
  char *dialect;
  enum ECILanguageDialect langID;
  char *charset;
} eciLocale;

static eciLocale eciLocales[] = { // +1 for a null element
  {"American_English", "en", "US", eciGeneralAmericanEnglish, "ISO-8859-1"},
  {"British_English", "en", "GB", eciBritishEnglish, "ISO-8859-1"},
  {"Castilian_Spanish", "es", "ES", eciCastilianSpanish, "ISO-8859-1"},
  {"Mexican_Spanish", "es", "MX", eciMexicanSpanish, "ISO-8859-1"},
  {"French", "fr", "FR", eciStandardFrench, "ISO-8859-1"},
  {"Canadian_French", "fr", "CA", eciCanadianFrench, "ISO-8859-1"},
  {"German", "de", "DE", eciStandardGerman, "ISO-8859-1"},
  {"Italian", "it", "IT", eciStandardItalian, "ISO-8859-1"},
  {"Mandarin_Chinese", "zh", "CN", eciMandarinChinese, "GBK"},
  {"Mandarin_Chinese GB", "zh", "CN_GB", eciMandarinChineseGB, "GBK"},
  {"Mandarin_Chinese PinYin", "zh", "CN_PinYin", eciMandarinChinesePinYin,"GBK"},
  {"Mandarin_Chinese UCS", "zh", "CN_UCS", eciMandarinChineseUCS, "UCS2"},
  {"Taiwanese_Mandarin", "zh", "TW", eciTaiwaneseMandarin, "BIG5"},
  {"Taiwanese_Mandarin Big 5", "zh", "TW_Big5", eciTaiwaneseMandarinBig5,"BIG5"},
  {"Taiwanese_Mandarin ZhuYin", "zh", "TW_ZhuYin",eciTaiwaneseMandarinZhuYin, "BIG5"},
  {"Taiwanese_Mandarin PinYin", "zh", "TW_PinYin",eciTaiwaneseMandarinPinYin, "BIG5"},
  {"Taiwanese_Mandarin UCS", "zh", "TW_UCS", eciTaiwaneseMandarinUCS, "UCS2"},
  {"Brazilian_Portuguese", "pt", "BR", eciBrazilianPortuguese, "ISO-8859-1"},
  {"Japanese", "ja", "JP", eciStandardJapanese, "SJIS"},
  {"Japanese_SJIS", "ja", "JP_SJIS", eciStandardJapaneseSJIS, "SJIS"},
  {"Japanese_UCS", "ja", "JP_UCS", eciStandardJapaneseUCS, "UCS2"},
  {"Finnish", "fi", "FI", eciStandardFinnish, "ISO-8859-1"},
  {NULL, 0, NULL}	
};

#define MAX_NB_OF_LANGUAGES (sizeof(eciLocales)/sizeof(eciLocales[0]) - 1)
#define ALLOCATED_MSG_LENGTH PIPE_MAX_BLOCK

static inote_error add_text(inote_tlv_t *tlv, void *user_data) {
  ENTER();
  
  struct engine_t *self = user_data;
  uint8_t *t = inote_tlv_get_value(tlv);
  inote_error ret = INOTE_OK;

  if (t && self && self->handle) {
    uint8_t x = t[tlv->length];
    t[tlv->length] = 0; // possible since (PIPE_MAX_BLOCK > TLV_MESSAGE_LENGTH_MAX)
    dbg("length=%d, text=%s", tlv->length, t);
    dbgText(t, tlv->length);
    Boolean eci_res = (uint32_t)eciAddText(self->handle, t);
    ret = (eci_res == ECITrue) ? INOTE_OK : INOTE_IO_ERROR;	
    t[tlv->length] = x;
    self->tlv_number++;
    dbg("tlv_number=%d", self->tlv_number);
  }
  
  return ret;
}

static inote_error add_capital(inote_tlv_t *tlv, bool capitals, void *user_data) {
  ENTER();

  struct engine_t *self = user_data;
  inote_error ret = INOTE_OK;

  if (self) {
    dbg("self=%p", self);
    if ((self->capital_mode == voxCapitalSoundIcon)
	&& (self->tlv_number)) {
      dbg("tlv_number=%x", self->tlv_number);
      // insert an index (except for the first tlv since an index
      // must follow text)
      uint32_t res;
      int index = self->tlv_number;
      index += capitals ? INDEX_CAPITALS : INDEX_CAPITAL;
      dbg("insert index = 0x%x", index);
      res = (uint32_t)eciInsertIndex(self->handle, index);
      if (!res) {
	dbg("error insert index");
      }
    }
    ret = add_text(tlv, user_data);
  }
  
  return ret;
}

static void engine_init_buffers(struct engine_t *self) {
  if (self) {
    self->tlv_message.buffer = self->tlv_message_buffer;
    self->tlv_message.end_of_buffer = self->tlv_message_buffer + sizeof(self->tlv_message_buffer);	  
    self->tlv_message.length = 0;
    self->cb.add_annotation = add_text;
    self->cb.add_charset = add_text;
    self->cb.add_punctuation = add_text;
    self->cb.add_text = add_text;  
    self->cb.add_capital = add_capital;  
    self->cb.user_data = self;  
  }
}

static struct engine_t *engine_create(void *handle)
{
  struct engine_t *self = NULL;
  
  ENTER();

  if (!handle)
    return NULL;
  
  self = (struct engine_t*)calloc(1, sizeof(*self));
  if (self) {
    self->id = ENGINE_ID;
    self->handle = handle;
    engine_init_buffers(self);
  } else {
    err("mem error (%d)", errno);
  }

  LEAVE();
  return self;
}

static void my_exit()
{
  struct msg_t msg;
  size_t l = sizeof(struct msg_t);
  int res;

  ENTER();

  memset(&msg, 0, MIN_MSG_SIZE);
  msg.id = MSG_EXIT;

  dbg("send exit msg");
  res = pipe_write(my_voxind->pipe_command, &msg, &l);
  if (res) {
    err("LEAVE, write error (%d)", res);
  }

  LEAVE();
}

static void sighandler(int sig) {
  err("signal=%s (%d)", strsignal(sig), sig);
  exit(EXIT_FAILURE);
}

static enum ECICallbackReturn my_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  size_t effective_msg_length = 0;
  size_t allocated_msg_length = 0;
  int res;
  uint32_t func_sav;
  struct engine_t *engine = (struct engine_t*)pData;
  static const char* msgString[] = {
    "eciWaveformBuffer",
    "eciPhonemeBuffer",
    "eciIndexReply",
    "eciPhonemeIndexReply",
    "eciWordIndexReply",
    "eciStringIndexReply",
    "eciAudioIndexReply",
    "eciSynthesisBreak"};
  const char *msgType;

  ENTER();
  
  if (!engine || !engine->cb_msg) {
    err("LEAVE, args error");
    return eciDataAbort;
  }

  engine->cb_msg->args.cb.lParam = 0;
  switch(Msg) {
  case eciWaveformBuffer:
    if (!engine->audio_sample_received) {
      size_t speech_len = 2*lParam;
      engine->audio_sample_received = 1;
      dbg("audio_sample_received=%d, first_tlv_type: 0x%02x",
	  engine->audio_sample_received,
	  engine->first_tlv_type);
      if (engine->capital_mode == voxCapitalSoundIcon) {
	if (engine->first_tlv_type == INOTE_TYPE_CAPITAL) {
	  engine->cb_msg->args.cb.lParam = MSG_PREPEND_CAPITAL;
	} else if (engine->first_tlv_type == INOTE_TYPE_CAPITALS) {
	  engine->cb_msg->args.cb.lParam = MSG_PREPEND_CAPITALS;
	}
      }
    }
    engine->cb_msg->effective_data_length = 2*lParam;  
    break;
  case eciPhonemeBuffer:
    engine->cb_msg->effective_data_length = lParam;  
    break;
  case eciIndexReply:
    {
      engine->cb_msg->effective_data_length = sizeof(uint32_t);
      engine->cb_msg->args.cb.lParam = htole32(lParam);
      uint32_t index = engine->cb_msg->args.cb.lParam;
      dbg("index=0x%02x", index);
      if (index & (INDEX_CAPITAL|INDEX_CAPITALS)) {
	if (engine->capital_mode == voxCapitalSoundIcon) {
	  bool is_capitals = !((index & INDEX_CAPITALS)^INDEX_CAPITALS);
	  Msg = eciWaveformBuffer;
	  engine->cb_msg->effective_data_length = lParam = 0;
	  engine->cb_msg->args.cb.lParam = is_capitals ? MSG_PREPEND_CAPITALS : MSG_PREPEND_CAPITAL;
	} else {
	  dbg("LEAVE, data processed (mode=%d)", engine->capital_mode);
	  return eciDataProcessed;
	}
      }
    }
    break;
  case eciPhonemeIndexReply:
  case eciWordIndexReply:
  case eciStringIndexReply:
  case eciSynthesisBreak:
    engine->cb_msg->effective_data_length = sizeof(uint32_t);
    engine->cb_msg->args.cb.lParam = htole32(lParam);
    break;
  default:
    err("LEAVE, unknown eci message (%d)", Msg);
    return eciDataAbort;	
  }

  msgType = msgString[Msg];
  effective_msg_length = MSG_HEADER_LENGTH + engine->cb_msg->effective_data_length;
  allocated_msg_length = engine->cb_msg_length;

  if (effective_msg_length > allocated_msg_length) {
    err("LEAVE, %s samples size error (%ld > %ld)", msgType, (long int)effective_msg_length, (long int)allocated_msg_length);
    return eciDataAbort;
  }
  
  engine->cb_msg->func = MSG_CB_WAVEFORM_BUFFER + Msg - eciWaveformBuffer;
  if (!msg_string(engine->cb_msg->func)) {
    err("LEAVE, unknown function (%d)", engine->cb_msg->func);
    return eciDataAbort;
  }
  engine->cb_msg->id = MSG_TO_APP_ID;
  ++engine->cb_msg->count;
    
  engine->cb_msg->res = 0;
  dbg("%s send cb msg '%s', length=%d, lParam=%08x, engine=%p (#%d)",
      msgType,
      msg_string(engine->cb_msg->func),
      engine->cb_msg->effective_data_length,
      engine->cb_msg->args.cb.lParam,
      engine,
      engine->cb_msg->count);
  res = pipe_write(my_voxind->pipe_command, engine->cb_msg, &effective_msg_length);
  if (res) {
    err("LEAVE, write error (%d)", res);
    return eciDataAbort;
  }

  effective_msg_length = MIN_MSG_SIZE;
  func_sav = engine->cb_msg->func;
  res = pipe_read(my_voxind->pipe_command, engine->cb_msg, &effective_msg_length);
  if (res) {
    err("LEAVE, read error (%d)",res);  
    return eciDataAbort;
  }

  if (engine->cb_msg->func != func_sav) {
    err("LEAVE, received func error (%d, expected=%d)", engine->cb_msg->func, func_sav);
    return eciDataAbort;
  }  

  dbg("recv msg '%s', length=%d, res=%d (#%d)", msg_string(engine->cb_msg->func),
      engine->cb_msg->effective_data_length, engine->cb_msg->res, engine->cb_msg->count);

  LEAVE();
  return engine->cb_msg->res;
}


static void set_output_buffer(struct voxind_t *v, struct engine_t *engine, struct msg_t *msg)
{
  size_t len = 0;
  size_t data_len = 0;
  int res;
  
  ENTER();

  if (!v || !msg) {
    err("LEAVE, args error(%d)",0);
    return;
  }

  msg->effective_data_length = 0;
  
  data_len = 2*msg->args.sob.nb_samples;
  len = MSG_HEADER_LENGTH + data_len;
  if (len > PIPE_MAX_BLOCK) {
    msg->res = ECIFalse;
    err("LEAVE, args error(%d)",1);
    return;
  }

  if (engine->cb_msg)
    free(engine->cb_msg);

  engine->cb_msg = calloc(1, len);
  if (!engine->cb_msg) {
    msg->res = ECIFalse;
    err("LEAVE, sys error(%d)", errno);
    return;
  }

  engine->cb_msg_length = len;
  dbg("create cb msg, data=%p, effective_data_length=%ld", engine->cb_msg->data, (long int)data_len);
  msg->res = (uint32_t)eciSetOutputBuffer(engine->handle, msg->args.sob.nb_samples, (short*)engine->cb_msg->data);  
}

static int check_engine(struct engine_t *engine)
{  
  return (engine && (engine->id == ENGINE_ID) && engine->handle);
}

int voxSetParam(void *handle, voxParam param, int value)
{
  dbg("ENTER(%p, 0x%0x, 0x%0x)", handle, param, value);
  int ret;
  struct engine_t *engine = handle;  

  if (!check_engine(engine))
    return -1;
  
  if (param == VOX_CAPITALS) {
    ret = engine->capital_mode;
    engine->capital_mode = value;
    dbg("capital_mode=%d", value);
  } else {
    ret = eciSetParam(engine->handle, param, value);
  }
  return ret;
}

static int unserialize(struct msg_t *msg, size_t *msg_length)
{
  uint32_t engine_index = -1;
  struct engine_t *engine = NULL;
  size_t length = 0;

  ENTER();

  if (!msg || !msg_length) {
    err("LEAVE, args error(%d)",0);
    return EINVAL;
  }

  engine_index = msg->engine;
  if (engine_index) {
    engine_index -= ENGINE_INDEX;
    if (engine_index >= ENGINE_MAX_NB) {
      msg("recv erroneous engine"); 
      return 0;
    }
  }

  engine = engines[engine_index];
  {
    int err = 0;
    if (*msg_length < MIN_MSG_SIZE) {
      msg("msg_length=%d (%d)", *msg_length, MIN_MSG_SIZE); 
      err=1;
    } else if (msg->id != MSG_TO_ECI_ID) {
      msg("id=%d (%d)", msg->id, MSG_TO_ECI_ID); 
      err=2;
    } else if (!msg_string(msg->func)) {
      msg("func=%d", msg->func); 
      err=3;
    } else if (*msg_length < MSG_HEADER_LENGTH + msg->effective_data_length) {
      msg("length=%d (%d)", *msg_length, MSG_HEADER_LENGTH + msg->effective_data_length); 
      err=4;
    } else if (engine && !check_engine(engine)) {
      msg("id=0x%x, h=0x%x",engine->id, (unsigned int)engine->handle);
      err=5;
    }

    if (err) {
      msg("recv erroneous msg (err=%d)", err); 
      memset(msg, 0, MIN_MSG_SIZE);
      msg->id = MSG_TO_APP_ID;
      msg->func = MSG_UNDEFINED;
      *msg_length = MIN_MSG_SIZE;
      msg->res = ECIFalse;
      dbg("send msg '%s', length=%d, res=0x%x (#%d)", msg_string(msg->func), msg->effective_data_length, msg->res, msg->count);    
      LEAVE();
      return 0;
    }
  }

  dbg("recv msg '%s', length=%d, engine=%p (#%d)", msg_string(msg->func), msg->effective_data_length, engine, msg->count); 
  
  msg->id = MSG_TO_APP_ID;

  // secu: check length, additional null terminator in case of C string
  length = min_size(msg->effective_data_length, ALLOCATED_MSG_LENGTH - MSG_HEADER_LENGTH - 1);
  msg->data[length] = 0;

  msg->effective_data_length = 0; 

  switch(msg->func) {
  case MSG_ADD_TLV: {
    if (!engine || (length > TLV_MESSAGE_LENGTH_MAX)) {
      msg->res = ECIFalse;	  
      break;
    }
    inote_slice_t *t = &(engine->tlv_message);
    t->buffer = msg->data;
    t->length = length;
    t->charset = INOTE_CHARSET_UTF_8; // TODO;
    t->end_of_buffer = t->buffer + t->length;
    dbg("data len=%lu", (long unsigned int)t->length);

    if (engine->tlv_number == 0) {
      engine->first_tlv_type = INOTE_TYPE_UNDEFINED;
      inote_slice_get_type(t, &engine->first_tlv_type);
      dbg("first_tlv_type: 0x%02x", engine->first_tlv_type);
    }

    dbg("calling inote_convert_tlv_to_text");
    int ret = inote_convert_tlv_to_text(t, &(engine->cb));	
    msg->res = (!ret) ? ECITrue : ECIFalse;
  }
    break;

  case MSG_ADD_TEXT:
    dbg("text=%s", (char*)msg->data);
    msg->res = (uint32_t)eciAddText(engine->handle, msg->data);
    break;

  case MSG_CLEAR_ERRORS:
    eciClearErrors(engine->handle);
    break;

  case MSG_CLEAR_INPUT:
    eciClearInput(engine->handle);
    break;

  case MSG_COPY_VOICE:
    msg->res = (uint32_t)eciCopyVoice(engine->handle, msg->args.cv.iVoiceFrom, msg->args.cv.iVoiceTo);
    break;

  case MSG_DELETE_DICT:
    msg->res = (uint32_t)eciDeleteDict(engine->handle, (char*)NULL + msg->args.dd.hDict);
    break;

  case MSG_ERROR_MESSAGE:
    BUILD_ASSERT(MSG_HEADER_LENGTH + MAX_ERROR_MESSAGE <= PIPE_MAX_BLOCK);
    eciErrorMessage(engine->handle, msg->data);
    msg->effective_data_length = MAX_ERROR_MESSAGE;
    msg("error=%s", (char*)msg->data);
    break;

  case MSG_GET_AVAILABLE_LANGUAGES: {
    struct msg_get_available_languages_t *lang = (struct msg_get_available_languages_t *)msg->data;
    BUILD_ASSERT(MSG_HEADER_LENGTH + sizeof(struct msg_get_available_languages_t) <= PIPE_MAX_BLOCK);
    lang->nb = sizeof(lang->languages)/sizeof(lang->languages[0]);
    msg->res = eciGetAvailableLanguages(lang->languages, &lang->nb);
    msg->effective_data_length = sizeof(struct msg_get_available_languages_t);
    dbg("nb lang=%d, msg->res=%d", lang->nb, msg->res);
  }
    break;

  case MSG_GET_DEFAULT_PARAM:
    msg->res = (uint32_t)eciGetDefaultParam(msg->args.gp.Param);
    break;

  case MSG_GET_DICT:
    msg->res = (uint32_t)eciGetDict(engine->handle);
    break;

  case MSG_GET_PARAM:
    msg->res = (uint32_t)eciGetParam(engine->handle, msg->args.gp.Param);
    break;

  case MSG_GET_VOICE_PARAM:
    msg->res = (uint32_t)eciGetVoiceParam(engine->handle, msg->args.gvp.iVoice, msg->args.gvp.Param);
    break;

  case MSG_INSERT_INDEX:
    msg->res = (uint32_t)eciInsertIndex(engine->handle, msg->args.ii.iIndex);
    break;

  case MSG_LOAD_DICT:
    dbg("hDict=%p, DictVol=0x%x, filename=%s", (char*)NULL + msg->args.ld.hDict, msg->args.ld.DictVol, msg->data);
    msg->res = eciLoadDict(engine->handle, (char*)NULL + msg->args.ld.hDict, msg->args.ld.DictVol, msg->data);
    break;

  case MSG_NEW:
    if (engine_number >= ENGINE_MAX_NB) {
      err("error: max number of engines allocated");
      return 0;
    }
    engines[engine_number] = engine_create(eciNew());
    // return index + ENGINE_INDEX (0 considered as error)
    engine_index = engines[engine_number] ? ENGINE_INDEX + engine_number: 0;
    engine_number++;
    msg->res = engine_index;	
    break;
	
  case MSG_NEW_DICT:
    msg->res = (uint32_t)eciNewDict(engine->handle);
    break;

  case MSG_NEW_EX:
    if (engine_number >= ENGINE_MAX_NB) {
      err("error: max number of engines allocated");
      return 0;
    }
    engines[engine_number] = engine_create(eciNewEx(msg->args.ne.Value));
    // return index + ENGINE_INDEX (0 considered as error)
    engine_index = engines[engine_number] ? ENGINE_INDEX + engine_number: 0;
    engine_number++;
    msg->res = engine_index;
    break;
    
  case MSG_PAUSE:
    msg->res = (uint32_t)eciPause(engine->handle, msg->args.p.On);
    break;

  case MSG_PROG_STATUS:
    msg->res = eciProgStatus(engine->handle);
    break;
    
  case MSG_REGISTER_CALLBACK: {
    ECICallback cb = NULL;
    if (msg->args.rc.Callback)
      cb = my_callback;
    dbg("engine=%p, handle=%p, cb=%p", engine, engine->handle, cb);
    eciRegisterCallback(engine->handle, cb, engine);
  }
    break;
    
  case MSG_RESET:
    eciReset(engine->handle);
    break;
    
  case MSG_SET_DEFAULT_PARAM:
    msg->res = (uint32_t)eciSetDefaultParam(msg->args.sp.Param, msg->args.sp.iValue);
    break;

  case MSG_SET_DICT:
    msg->res = (uint32_t)eciSetDict(engine->handle, (char*)NULL + msg->args.sd.hDict);
    break;

  case MSG_SET_OUTPUT_DEVICE:
    msg->res = (uint32_t)eciSetOutputDevice(engine->handle, msg->args.sod.iDevNum);
    break;

  case MSG_SET_PARAM:
    dbg("handle=%p, param=0x%0x, value=0x%0x)", engine->handle, msg->args.sp.Param, msg->args.sp.iValue);
    msg->res = (uint32_t)eciSetParam(engine->handle, msg->args.sp.Param, msg->args.sp.iValue);
    break;

  case MSG_VOX_SET_PARAM:
    msg->res = (uint32_t)voxSetParam(engine, msg->args.sp.Param, msg->args.sp.iValue);
    break;

  case MSG_SET_VOICE_PARAM:
    msg->res = (uint32_t)eciSetVoiceParam(engine->handle, msg->args.svp.iVoice, msg->args.svp.Param, msg->args.svp.iValue);
    break;

  case MSG_SET_OUTPUT_BUFFER:
    set_output_buffer(my_voxind, engine, msg);
    break;

  case MSG_SET_OUTPUT_FILENAME:
    msg->res = (uint32_t)eciSetOutputFilename(engine->handle, msg->data);
    break;

  case MSG_SYNTHESIZE:
    msg->res = (uint32_t)eciSynthesize(engine->handle);
    break;

  case MSG_SYNCHRONIZE:
    msg->res = (uint32_t)eciSynchronize(engine->handle);
    engine->tlv_number = 0;
    engine->first_tlv_type = INOTE_TYPE_UNDEFINED;
    engine->audio_sample_received = 0;
    dbg("tlv_number=%d, audio_sample_received=%d, first_tlv_type: 0x%02x",
	engine->tlv_number,
	engine->audio_sample_received,
	engine->first_tlv_type);
    break;

  case MSG_SPEAKING:
    msg->res = (uint32_t)eciSpeaking(engine->handle);
    break;

  case MSG_STOP:
    msg->res = (uint32_t)eciStop(engine->handle);
    break;

  case MSG_VERSION:
    BUILD_ASSERT(MSG_HEADER_LENGTH + MAX_VERSION <= PIPE_MAX_BLOCK);
    eciVersion(msg->data);
    msg->effective_data_length = MAX_VERSION;
    dbg("version=%s", msg->data);
    break;
    
  case MSG_GET_VERSIONS: {
    struct msg_get_versions_t *data = (struct msg_get_versions_t *)msg->data;
    BUILD_ASSERT(MSG_HEADER_LENGTH + sizeof(struct msg_get_versions_t) <= PIPE_MAX_BLOCK);
    data->magic = MSG_GET_VERSIONS_MAGIC;
    data->msg = MSG_API;
    data->voxin = (LIBVOXIN_VERSION_MAJOR<<16) + (LIBVOXIN_VERSION_MINOR<<8) + LIBVOXIN_VERSION_PATCH;
    data->inote = (INOTE_VERSION_MAJOR<<16) + (INOTE_VERSION_MINOR<<8) + (INOTE_VERSION_PATCH);
    char ver[20];
    eciVersion(ver);
    data->tts = 0;
    { // convert ver="6.7.4" to 0x060704
      char *s, *sav = NULL;
      int i;
      s = strtok_r(ver, ".", &sav);
      for (i=0; i<3 && s; i++) {
	data->tts<<8;
	if (s) {
	  data->tts += atoi(s);
	  s = strtok_r(NULL, ".", &sav);
	}
      }
    }
    
    msg->res = 0;	
    msg->effective_data_length = sizeof(struct msg_get_versions_t);
    dbg("versions: msg=%08x, libvoxin=%08x, libinote=%08x, tts=%08x",
	data->msg, data->voxin, data->inote, data->tts);
  }
    break;
    
  case MSG_VOX_GET_VOICES: {
    struct msg_vox_get_voices_t *data = (struct msg_vox_get_voices_t *)msg->data;
    uint32_t languages[MSG_VOX_LIST_MAX];
    uint32_t nb;
    BUILD_ASSERT(MSG_HEADER_LENGTH + sizeof(struct msg_vox_get_voices_t) <= PIPE_MAX_BLOCK);
    nb = MSG_VOX_LIST_MAX;
    data->nb = 0;
    msg->res = eciGetAvailableLanguages(NULL, &nb);
    memset(languages, 0, sizeof(languages));
    if (nb >= MSG_VOX_LIST_MAX)
      nb = MSG_VOX_LIST_MAX;
    msg->res = eciGetAvailableLanguages(languages, &nb);

    dbg("nb=%d, msg->res=%d", nb, msg->res);
	
    if (!msg->res && nb) {
      int i;
      for (i=0; i<nb; i++) {
	int j;
	dbg("languages[%d]=0x%0x", i, languages[i]);
	for (j=0; j<MAX_NB_OF_LANGUAGES; j++) {
	  dbg("eciLocales[%d].langID=0x%0x", j, eciLocales[j].langID);
	  if (languages[i] != eciLocales[j].langID)
	    continue;
		  
	  struct msg_vox_t *vox = data->voices + data->nb;
	  eciLocale *eci = eciLocales+j;		  
	  data->nb++;

	  vox->id = eci->langID;		  
	  vox->tts_id = MSG_TTS_ECI;		  

	  strncpy(vox->name, eci->name, MSG_VOX_STR_MAX);
	  vox->name[MSG_VOX_STR_MAX-1] = 0;

	  strncpy(vox->lang, eci->lang, MSG_VOX_STR_MAX);
	  vox->lang[MSG_VOX_STR_MAX-1] = 0;

	  strncpy(vox->variant, eci->dialect, MSG_VOX_STR_MAX);
	  vox->variant[MSG_VOX_STR_MAX-1] = 0;

	  vox->rate = 11025;
	  vox->size = 16;

	  strncpy(vox->charset, eci->charset, MSG_VOX_STR_MAX);
	  vox->charset[MSG_VOX_STR_MAX-1] = 0;

	  /* gender, age, multilang, quality */
	  break;
	}
      }
    }
	
    msg->res = 0;	
    msg->effective_data_length = sizeof(struct msg_vox_get_voices_t);
    dbg("nb voices=%d, msg->res=%d", data->nb, msg->res);
  }
    break;

  default:
    msg->res = ECIFalse;
    break;
  }

  *msg_length = MSG_HEADER_LENGTH + msg->effective_data_length;
  
 exit0:
  dbg("send msg '%s', length=%d, res=0x%x (#%d)", msg_string(msg->func), msg->effective_data_length, msg->res, msg->count);    
  LEAVE();
  return 0;
}


#ifdef DEBUG
#define VOXIND_DBG "/tmp/test_voxind"
#endif

int main(int argc, char **argv)
{
  int res;
  int i;
  struct voxind_t *v;
  struct sigaction act;
  
#ifdef DEBUG
  {
    struct stat buf;
    while (!stat(VOXIND_DBG, &buf)) {
      sleep(1);
    }
  }
#endif

  ENTER();
  BUILD_ASSERT(PIPE_MAX_BLOCK > MIN_MSG_SIZE);
  BUILD_ASSERT(PIPE_MAX_BLOCK > TLV_MESSAGE_LENGTH_MAX);
  
  /* memset(&act, 0, sizeof(act)); */
  /* sigemptyset(&act.sa_mask); */
  /* act.sa_flags = 0; */
  /* act.sa_handler = sighandler; */
  /* for (i = 1; i < NSIG; i++) { */
  /*   sigaction(i, &act, NULL); */
  /* } */

  dbg("voxin api=%d.%d.%d", LIBVOXIN_VERSION_MAJOR, LIBVOXIN_VERSION_MINOR, LIBVOXIN_VERSION_PATCH);

  my_voxind = calloc(1, sizeof(struct voxind_t));
  if (!my_voxind) {
    res = errno;
    goto exit0;
  }

  my_voxind->msg = calloc(1, PIPE_MAX_BLOCK);
  if (!my_voxind->msg) {
    res = errno;
    goto exit0;
  }
  my_voxind->msg_length = PIPE_MAX_BLOCK;
  
  res = pipe_restore(&my_voxind->pipe_command, PIPE_COMMAND_FILENO, READ_TIMEOUT_IN_MS);
  if (res)
    goto exit0;

  atexit(my_exit);
  
  do {
    size_t msg_length = my_voxind->msg_length;
    if(pipe_read(my_voxind->pipe_command, my_voxind->msg, &msg_length))
      goto exit0;
    if (unserialize(my_voxind->msg, &msg_length))
      goto exit0;
    pipe_write(my_voxind->pipe_command, my_voxind->msg, &msg_length);    
  } while (1);

 exit0:
  err("LEAVE, (err=%d)",res);
  return res;
}

/* local variables: */
/* c-basic-offset: 2 */
/* end: */

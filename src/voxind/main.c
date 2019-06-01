#define _GNU_SOURCE
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include "msg.h"
#include "pipe.h"
#include "debug.h"
#include "eci.h"
#include "inote.h"

#define VOXIND_ID 0x05000A01 
#define ENGINE_ID 0x15000A01 

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
};

static inote_error add_text(inote_tlv_t *tlv, void *user_data) {
  ENTER();

  ECIHand handle = (ECIHand *)user_data;  
  uint8_t *t = inote_tlv_get_value(tlv);
  inote_error ret = INOTE_OK;

  if (t && handle) {
	uint8_t x = t[tlv->length];
	t[tlv->length] = 0; // possible since (PIPE_MAX_BLOCK > TLV_MESSAGE_LENGTH_MAX)
	dbg("length=%d, text=%s", tlv->length, t);
	dbgText(t, tlv->length);
	Boolean eci_res = (uint32_t)eciAddText(handle, t);
	ret = (eci_res == ECITrue) ? INOTE_OK : INOTE_IO_ERROR;	
	t[tlv->length] = x;
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
	self->cb.user_data = self->handle;  
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

  switch(Msg) {
  case eciWaveformBuffer:
	engine->cb_msg->effective_data_length = 2*lParam;  
	break;
  case eciPhonemeBuffer:
	engine->cb_msg->effective_data_length = lParam;  
	break;
  case eciIndexReply:
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
    err("LEAVE, %s samples size error (%d > %d)", msgType, effective_msg_length, allocated_msg_length);
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
  dbg("%s send cb msg '%s', length=%d, engine=%p (#%d)", msgType, msg_string(engine->cb_msg->func),
      engine->cb_msg->effective_data_length, engine, engine->cb_msg->count);
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
  dbg("create cb msg, data=%p, effective_data_length=%d", engine->cb_msg->data, data_len);
  msg->res = (uint32_t)eciSetOutputBuffer(engine->handle, msg->args.sob.nb_samples, (short*)engine->cb_msg->data);  
}

static int check_engine(struct engine_t *engine)
{  
  return (engine && (engine->id == ENGINE_ID) && engine->handle);
}

static int unserialize(struct msg_t *msg, size_t *msg_length)
{
  struct engine_t *engine = NULL;
  size_t length = 0;

  ENTER();

  if (!msg || !msg_length) {
    err("LEAVE, args error(%d)",0);
    return EINVAL;
  }

  engine = (struct engine_t*)msg->engine;
  if ((*msg_length < MIN_MSG_SIZE)
      || (msg->id != MSG_TO_ECI_ID)
      || !msg_string(msg->func)
      || (*msg_length < MSG_HEADER_LENGTH + msg->effective_data_length)
      || (engine && !check_engine(engine))) {
    msg("recv erroneous msg"); 
    memset(msg, 0, MIN_MSG_SIZE);
    msg->id = MSG_TO_APP_ID;
    msg->func = MSG_UNDEFINED;
    *msg_length = MIN_MSG_SIZE;
    msg->res = ECIFalse;
    dbg("send msg '%s', length=%d, res=0x%x (#%d)", msg_string(msg->func), msg->effective_data_length, msg->res, msg->count);    
    LEAVE();
    return 0;
  }

  dbg("recv msg '%s', length=%d, engine=%p (#%d)", msg_string(msg->func), msg->effective_data_length, engine, msg->count); 
  
  msg->id = MSG_TO_APP_ID;
  length = msg->effective_data_length;
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
	engine = engine_create(eciNew());
    msg->res = (uint32_t)engine;
    break;

  case MSG_NEW_DICT:
    msg->res = (uint32_t)eciNewDict(engine->handle);
    break;

  case MSG_NEW_EX:
	engine = engine_create(eciNewEx(msg->args.ne.Value));
    msg->res = (uint32_t)engine;
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
    msg->res = (uint32_t)eciSetParam(engine->handle, msg->args.sp.Param, msg->args.sp.iValue);
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
  
  res = pipe_restore(&my_voxind->pipe_command, PIPE_COMMAND_FILENO);
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

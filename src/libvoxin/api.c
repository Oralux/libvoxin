#include "puncfilter.h"

extern "C" {

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include "eci.h"
#include "debug.h"
#include "conf.h"
#include "libvoxin.h"
#include "msg.h"

#define FILTER_SSML 1
#define FILTER_PUNC 2

#define ENGINE_ID 0x020A0005
#define IS_ENGINE(e) (e && (e->id == ENGINE_ID) && e->handle && e->api)

  struct engine_t {
    uint32_t id; // structure identifier
    struct api_t *api; // parent api
    uint32_t handle; // eci handle
    void *cb; // user callback
    void *data_cb; // user data callback
    int16_t *samples; // user samples buffer
    uint32_t nb_samples; // current number of samples in the user sample buffer
    uint32_t stop_required;
    // from eciAddText
    uint32_t text_count;
    uint32_t filters;
    void *punc;
    enum charset_t charset;
  };

#define ALLOCATED_MSG_LENGTH PIPE_MAX_BLOCK

  struct api_t {
    libvoxin_handle_t my_instance; // communication channel with voxind. my_api is fully created when my_instance is non NULL 
    pthread_mutex_t stop_mutex; // to process only one stop command
    pthread_mutex_t api_mutex; // to process exclusively any other command
    struct msg_t *msg; // message for voxind
  };
  
  static struct api_t my_api = {0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, NULL};

  static enum charset_t getCharset(enum ECILanguageDialect lang)
  {
    enum charset_t charset = CHARSET_UNDEFINED;
    if ((lang <= eciStandardItalian)
	|| (lang == eciBrazilianPortuguese)
	|| (lang == eciStandardFinnish)) {
      return CHARSET_ISO_8859_1; // 1 byte
    }

    switch(lang) {
    case eciMandarinChinese:
    case eciMandarinChinesePinYin:
    case eciStandardCantonese:
      charset = CHARSET_GBK; // 1 or 2 bytes
      break;
    case eciMandarinChineseUCS:
    case eciTaiwaneseMandarinUCS:
    case eciStandardJapaneseUCS:
    case eciStandardCantoneseUCS:
    case eciHongKongCantoneseUCS:
      charset = CHARSET_UCS_2; // 2 bytes
      break;
    case eciTaiwaneseMandarin:
    case eciTaiwaneseMandarinZhuYin:
    case eciTaiwaneseMandarinPinYin:
    case eciHongKongCantonese:
      charset = CHARSET_BIG_5; // 1 or 2 bytes
      break;
    case eciStandardJapanese:
      charset = CHARSET_SJIS; // 1 or 2 bytes
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
    api->msg = (msg_t*)calloc(1, PIPE_MAX_BLOCK);  
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


  static int msg_copy_data(struct msg_t *msg, const char *text)
  {
    char *c = NULL;
    int res = 0;
    int effective_msg_length;
  
    ENTER();
    if (!msg || !text) {
      err("LEAVE, args error");
      return EINVAL;
    }

    c = (char*)memccpy(msg->data, text, 0, ALLOCATED_MSG_LENGTH - MSG_HEADER_LENGTH - 1);
    if (c == NULL) {
      err("LEAVE, args error");
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
  int process_func1(struct api_t* api, struct msg_t *header, const char* data,
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
  

  static struct engine_t *engine_create(uint32_t handle, struct api_t *api)
  {
    struct engine_t *engine = NULL;
    int res;
  
    ENTER();

    engine = (engine_t*)calloc(1, sizeof(*engine));
    if (engine) {
      engine->id = ENGINE_ID;
      engine->handle = handle;
      engine->api = api;
    } else {
      err("mem error (%d)", errno);
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

    puncfilter_delete(engine->punc);
  
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

    engine->charset = CHARSET_UNDEFINED;
    msg_set_header(&header, MSG_GET_PARAM, engine->handle);
    header.args.gp.Param = eciLanguageDialect;
    res = process_func1(engine->api, &header, NULL, &eci_res, false, false);
    if (!res) {
      engine->charset = getCharset((ECILanguageDialect)eci_res);
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
      enum ECILanguageDialect lang;
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
    struct api_t *api;
  
    dbg("ENTER(%p,%d,%p)", hEngine, iSize, psBuffer);
 
    if (!IS_ENGINE(engine)) {
      err("LEAVE, args error");
      return eci_res;
    }

    api = engine->api;
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

    ENTER();

    if (!IS_ENGINE(engine)) {
      err("LEAVE, args error");
      return ECIFalse;
    }

    msg_set_header(&header, MSG_SET_OUTPUT_FILENAME, engine->handle);
    process_func1(engine->api, &header, (const char*)pFilename, &eci_res, true, true);
    return eci_res;
  }


  Boolean eciAddText(ECIHand hEngine, ECIInputText pText)
  {
    Boolean eci_res = ECIFalse;
    struct engine_t *engine = (struct engine_t *)hEngine;
    struct msg_t header;
    const char *filteredText = (const char *)pText;
    dbg("ENTER(%p,%p)", hEngine, pText);

    if (!IS_ENGINE(engine)) {
      err("LEAVE, args error");
      return ECIFalse;
    }
  
    engine->text_count++;
    // TODO locales
    if ((engine->text_count <= 2) && (!strncmp((const char *)pText, " `gfa", 5))) {
      char *c = (char*)pText + 5; 
      switch(*c) {
      case '1':
	dbg("Filter SSML");
	engine->filters |= FILTER_SSML;
	// use the minimal xml filter in puncfilter 
	//break;
      case '2':
	dbg("Filter PUNC");
	if (!(engine->filters & FILTER_PUNC)) {
	  engine->filters |= FILTER_PUNC;
	  engine->punc = puncfilter_create();
	}
	return ECITrue;
      default:
	break;
      }
    }
  
    if (engine->punc) {
      puncfilter_do(engine->punc, (const char*)pText, engine->charset, &filteredText);
    }

    msg_set_header(&header, MSG_ADD_TEXT, engine->handle);
    process_func1(engine->api, &header, filteredText, &eci_res, true, true);

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
      if (engine->cb && engine->samples
	  && (m->effective_data_length <= 2*engine->nb_samples)) {
	ECICallback cb = (ECICallback)engine->cb;
	enum ECIMessage Msg = (enum ECIMessage)(m->func - MSG_CB_WAVEFORM_BUFFER + eciWaveformBuffer);
	dbg("call user callback, handle=0x%x, msg=%s, #samples=%d",
	    engine->handle, msg_string((msg_type)(m->func)), m->effective_data_length/2);	  
	memcpy(engine->samples, m->data, m->effective_data_length);
	m->res = (enum ECICallbackReturn)cb((ECIHand)((char*)NULL+engine->handle), Msg, m->effective_data_length/2, engine->data_cb);
      } else {
	err("error callback, handle=0x%x, msg=%s, #samples=%d",
	    engine->handle, msg_string((msg_type)(m->func)), m->effective_data_length/2);
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
    struct api_t *api;
    int res;
  
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
    int res;
    
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
    int res;
    struct api_t *api;
  
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
	  aLanguages[i] = (ECILanguageDialect)(lang->languages[i]);
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
		engine->charset = getCharset(Value);
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
    if (!process_func1(engine->api, &header, NULL, &eci_res, false, true)) {
      if (Param == eciLanguageDialect) {
	engine->charset = getCharset((enum ECILanguageDialect)iValue);
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
    struct api_t *api;
    int res = 0;  
   
    dbg("ENTER(%d)", Param);  

    if (!IS_ENGINE(engine)) {
      err("LEAVE, args error");
      return eci_res;
    }

    api = engine->api;

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
  
    ENTER();

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

    process_func1(engine->api, &header, (const char*)pFilename, (int*)&eci_res, true, true);
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
    struct api_t *api;
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

}

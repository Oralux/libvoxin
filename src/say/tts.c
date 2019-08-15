#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include "tts.h"
#include "voxin.h"
#include "debug.h"

#define MAX_SAMPLES 10240
#define VOICE_ID_UNDEFINED -1

typedef struct {
  vox_t voice[VOX_RESERVED_VOICES];  
  unsigned int len; // effective number of elements in the voice array
} voice_t;

typedef struct {
  void *handle;
  short samples[MAX_SAMPLES];
  int speed; // SPEED_UNDEFINED if unset
  voice_t *voice;
  int id; // obtained by voiceGetId, used to retreive the features of a voice from the voice object
  char tempbuf[MAX_CHAR+10];
} tts_t;

typedef struct {
  void *wav;
  int part;
  tts_t *tts;
} data_cb_t;

static enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData) {
  data_cb_t *data_cb = (data_cb_t *)pData;

  if (data_cb && data_cb->tts && (Msg == eciWaveformBuffer)) {
	wavfileWriteData(data_cb->wav, data_cb->part, (uint8_t*)data_cb->tts->samples, 2*lParam);
  }
  return eciDataProcessed;
}

static voice_t *voiceCreate() {
  ENTER();
  int i, j;
  voice_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;
  
  self->len = VOX_RESERVED_VOICES;
  
  if (voxGetVoices(self->voice, &self->len)) {
	free(self);
	return NULL;
  }

  for (i=0, j=VOX_ECI_VOICES; i<self->len; i++) {
	size_t len = 0;
	vox_t *vox = self->voice + i;

	dbg("voxin: vox[%d]=id=0x%x, name=%s, lang=%s, variant=%s, charset=%s", i, vox->id, vox->name, vox->lang, vox->variant, vox->charset);				
	len = strnlen(vox->name, VOX_STR_MAX-1);
	if (!*vox->variant)
	  strcpy(vox->variant, "none");
	  
	/* convert the name to lower case and add quality */
	{
	  int k;
	  for (k=0; k<len; k++) {
		vox->name[k] = tolower(vox->name[k]);
	  }
	  if (*vox->quality && (len < sizeof(vox->name))) {
		snprintf(vox->name+len, sizeof(vox->name)-len, "-%s", vox->quality);
		vox->name[sizeof(vox->name)-1] = 0;
	  }
	}
				
	vox->rate = 22050; //TODO (to remove)
	dbg("voxin: local[%d]=langID=0x%x, name=%s, lang=%s, dialect=%s, charset=%s, rate=%d",
		i, vox->id, vox->name, vox->lang, vox->variant, vox->charset, vox->rate);
	j++;
  }
  return self;  
}

static void voiceDelete(voice_t *self) {
  ENTER();
  if (!self)
	return;
  
  self->len = 0;
  free(self);
}

// voiceGetId returns the voice id or -1 if not found
static int voiceGetId(voice_t *self, const char *name) {
  ENTER();
  int i;
  int index = VOICE_ID_UNDEFINED;
  
  if (!self || !name || !*name) {
	goto exit0;
  }

  for (i = 0; i < self->len; i++) {
	if (!strcasecmp(name, self->voice[i].name) || !strcasecmp(name, self->voice[i].lang)) {
	  index = i;
	  break;
	}
  }
	
 exit0:
  return index;	
}

// voiceGetRate returns the voice rate or 11025 if not found
static int voiceGetRate(voice_t *self, unsigned int id) {
  return (self && (id < self->len)) ? self->voice[id].rate : 11025;
}

// voiceGetLangID returns the lang id or 0 if not found
static int voiceGetLangID(voice_t *self, unsigned int id) {
  return (self && (id < self->len)) ? self->voice[id].id : 0;
}


void *ttsCreate(const char *voiceName, int speed) {
  ENTER();
  tts_t *self = calloc(1, sizeof(*self));  
  self->voice = voiceCreate();
  self->id = voiceGetId(self->voice, voiceName);
  self->speed = speed;
  return self;
}

void ttsDelete(void *handle) {
  ENTER();
  tts_t *self = handle;
  if (!self)
	return;
  voiceDelete(self->voice);
  self->voice = NULL;
  free(self);
}

int ttsSetVoice(void *handle, unsigned int id) {
  ENTER();
  tts_t *self = handle;
  int res = 1;
  int langId;
  if (!self || !self->handle)
	return 1;

  self->id = id;
  langId = voiceGetLangID(self->voice, id);
  
  if (langId && eciSetParam(self->handle, eciLanguageDialect, langId) != -1) {
	res = 0;
  } else {
	err("error: set param %d to %d", eciLanguageDialect, langId);
  }
  return res;
}

int ttsInit(void *handle, void *wav, int part) {
  ENTER();

  tts_t *self = handle;
  static data_cb_t data_cb;
  int err = 0;

  if (!self || !wav) {
	err = EINVAL;
	goto exit0;
  }

  if (self->handle)
	return 0;

  self->handle = eciNew();
  if (!self->handle) {
	err("null handle");
	return EIO;
  }

  data_cb.wav = wav;
  data_cb.part = part;
  data_cb.tts = self;

  // enable dictionaries
  eciSetParam(self->handle, eciDictionary, 0);

  /* enable ssml and punctuation filters */
  eciSetParam(self->handle, eciInputType, 1);
  eciAddText(self->handle, " `gfa1 ");
  eciAddText(self->handle, " `gfa2 ");

  err = EIO;
  if (ttsSetVoice(self, self->id))
	goto exit0;

  if (self->speed != SPEED_UNDEFINED) {
	if (eciSetVoiceParam(self->handle, 0, eciSpeed, self->speed) == -1) {
	  err("error: set voice param %d to %d", eciSpeed, self->speed);
	  goto exit0;
	}
  }

  eciRegisterCallback(self->handle, my_client_callback, &data_cb);

  if (!eciSetOutputBuffer(self->handle, MAX_SAMPLES, self->samples)) {
	goto exit0;
  }

  return 0;
  
 exit0:
  if (self->handle) {
	eciDelete(self->handle);
  }
  if (err) {
	err("%s", strerror(err));
  }
  return err;
}

int ttsGetRate(void *handle) {
  ENTER();
  tts_t *self = handle;

  if (!self)
	return 1;

  return voiceGetRate(self->voice, self->id);
}

int ttsSay(void *handle, const char *text) {
  ENTER();
  tts_t *self = handle;
  int err = EINVAL;
  if (!self)
	return err;
  if (!eciAddText(self->handle, text)) {
	self->tempbuf[16] = 0;
	err("Error: add text=%s...", text);
  } else if (!eciSynthesize(self->handle)) {
	err("Error: synth handle=0x%p", text);
  } else if (!eciSynchronize(self->handle)) {
	err("Error: sync handle=0x%p", text);
  } else {
	err = 0;
  }
  return err;
}

int ttsPrintList(void *handle) {
  ENTER();
  int i;
  voice_t *v;  
  tts_t *self = handle;
  int err = EINVAL;

  if (!self || !self->voice)
	return err;

  v = self->voice;
  printf("Name,Language,Variant\n");
  for (i=0; i<v->len; i++) {
	printf("%s,%s,%s\n", v->voice[i].name, v->voice[i].lang, v->voice[i].variant);
  }
  
  return 0;
}


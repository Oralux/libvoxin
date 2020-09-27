// Capital letters + insert index/marker
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "voxin.h"


#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];


const char* text[] = {
  "one letter",
  "Capital letter",
  "one Capital ",
  "Capital Letter, ",
  "CAPITALS ",
};

#define MAX_TEXT (sizeof(text)/sizeof(*text))

typedef struct {
  int fd;
} data_cb_t;

static data_cb_t data_cb;


#define nveEnglishNathan 0x2d0000
#define nveFrenchThomas 0x380000
#define nveSpanishMarisol 0x340000

#define nve_lang_id nveEnglishNathan
#define eci_lang_id eciGeneralAmericanEnglish
//#define eci_lang_id eciStandardFrench

static int test(void *handle);
static enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData);
static int get_voices();
  
int main(int argc, char** argv) {
  int voice[] = {eci_lang_id};
  //  int voice[] = {nve_lang_id};
  //  int voice[] = {eci_lang_id, nve_lang_id};
  int i;
  int voice_max = sizeof(voice)/sizeof(*voice);

  get_voices();
  
  ECIHand handle = eciNew();
  if (!handle)
    return __LINE__;

  data_cb.fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (data_cb.fd == -1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
  
  for (i=0; i<voice_max; i++) {
    int err;

    if (voxSetParam(handle, VOX_LANGUAGE_DIALECT, voice[i]) == VOX_PARAM_OUT_OF_RANGE)
      return err;

    err = test(handle);
    if (err)
      return err;

    if (voxSetParam(handle, VOX_CAPITALS, voxCapitalSoundIcon) == VOX_PARAM_OUT_OF_RANGE)
      return __LINE__;
      
    err = test(handle);
    if (err)
      return err;

    if (voxSetParam(handle, VOX_CAPITALS, voxCapitalNone) == VOX_PARAM_OUT_OF_RANGE)
      return __LINE__;
  }

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
  return 0;
}

static enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  data_cb_t *data_cb = (data_cb_t *)pData;

  switch(Msg) {
  case eciWaveformBuffer:
    if (data_cb) {
      write(data_cb->fd, my_samples, 2*lParam);
    }
    break;
  case eciIndexReply:
    fprintf(stderr, "index reply=0x%08lx\n", lParam);
    break;
  default:
    break;
  }
    
  return eciDataProcessed;
}

static int get_voices() {
  int i;
  vox_t *voice = NULL;
  unsigned int number_of_voices = 0;

  if (voxGetVoices(NULL, &number_of_voices) || !number_of_voices) {
    return __LINE__;
  }

  voice = calloc(1, number_of_voices*sizeof(*voice));
  if (voxGetVoices(voice, &number_of_voices)) {
    return __LINE__;
  }

  fprintf(stderr, "      id               name        lang variant quality\n");
  for (i=0; i<number_of_voices; i++) {
    fprintf(stderr, "%02d 0x%08x %20s %s   %s      %s\n",
	    i, voice[i].id, voice[i].name, voice[i].lang, voice[i].variant, voice[i].quality);
  }

  free(voice);
  return 0;
}

static int test(void *handle) {
  int i;
  
  for (i=0; i<MAX_TEXT; i++) {
    fprintf(stderr,"add index %d\n", i);
    if (!eciInsertIndex(handle, i))
      return __LINE__;

    fprintf(stderr,"add text %s\n", text[i]);
    if (eciAddText(handle, text[i]) == ECIFalse)
      return __LINE__;    
  }

  fprintf(stderr,"add index %d\n", i);
  if (!eciInsertIndex(handle, i))
	return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;
  return 0;
}


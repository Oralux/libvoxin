// get voice list
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
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

const char* vh_quote = "So long as there shall exist, by virtue of law and custom";

typedef struct {
  int fd;
} data_cb_t;

static data_cb_t data_cb;

enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  data_cb_t *data_cb = (data_cb_t *)pData;

  if (data_cb && (Msg == eciWaveformBuffer))
    {
	  write(data_cb->fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}

int main(int argc, char** argv)
{
  /* { */
  /*   struct stat buf; */
  /*   while (!stat(TEST_DBG, &buf)) { */
  /*     sleep(1); */
  /*   } */
  /* } */

  enum {NB_VOICES=30};
  vox_t list[NB_VOICES];
  int nbVoices = NB_VOICES;
  int res = voxGetVoices(list, &nbVoices);
  
  const vox_t *v;
  int i;
  uint32_t id_compact = 0;
  uint32_t id_high = 0;
  for (i=0; i<nbVoices; i++) {
	vox_t *v = list+i;
	if (!strcmp(v->lang, "en") && !strcmp(v->quality, "embedded-high"))
	  id_high = v->id;
	if (!strcmp(v->lang, "en") && !strcmp(v->quality, "embedded-compact"))
	  id_compact = v->id;
  }

  ECIHand handle;
  /* if (i >= nLanguages) { */
  handle = eciNew();
  /* } else { */
  /* 	handle = eciNewEx(v->id); */
  /* } */
  if (!handle)
    return __LINE__;

  data_cb.fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (data_cb.fd == -1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
   
  //  eciSetOutputFilename(handle, PATHNAME_RAW_DATA);

  eciSetParam(handle, eciLanguageDialect, id_compact);  
  if (eciAddText(handle, vh_quote) == ECIFalse)
    return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  eciSetParam(handle, eciLanguageDialect, id_high);  
  if (eciAddText(handle, vh_quote) == ECIFalse)
    return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
  return 0;
}

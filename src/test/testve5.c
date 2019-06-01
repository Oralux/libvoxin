// eciSynchronize: nominal sequence + insert index/marker
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
#include "eci.h"


#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];


const char* vh_quote[] = {
  "in ",
  "other ",
  "words, ",
  "and ",
  "with ",
  "a ",
  "still ",
  "wider ",
  "significance ",
  ", ",
  "so long as ignorance and poverty exist on earth, ",
  "books of the nature of Les Miserables cannot fail to be of use.",
  " ",
  "HAUTEVILLE HOUSE, 1862."
};

#define MAX_VH_QUOTE (sizeof(vh_quote)/sizeof(*vh_quote))

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
  /* uint8_t *buf; */
  /* size_t len; */
  int i;
  
  /* { */
  /*   struct stat buf; */
  /*   while (!stat(TEST_DBG, &buf)) { */
  /*     sleep(1); */
  /*   } */
  /* } */

  
  ECIHand handle = eciNew();
  if (!handle)
    return __LINE__;

  data_cb.fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (data_cb.fd == -1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
   
  //  eciSetOutputFilename(handle, PATHNAME_RAW_DATA);


  for (i=0; i<MAX_VH_QUOTE; i++) {
    if (!eciInsertIndex(handle, i*10000))
      return __LINE__;

    if (eciAddText(handle, vh_quote[i]) == ECIFalse)
      return __LINE__;    
  }
  /* if (eciSynthesize(handle) == ECIFalse) */
  /*   return __LINE__; */

  if (!eciInsertIndex(handle, 0))
	return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
  return 0;
}

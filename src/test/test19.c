// unexpected 'lower than' character in ssml mode
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

const char* unexpected_lower_than_character = "unexpected < character in SSML mode";

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
  {
    int major=0, minor=0, patch=0;
    voxGetVersion(&major, &minor, &patch);
    if ((major != LIBVOXIN_VERSION_MAJOR)
	|| (minor != LIBVOXIN_VERSION_MINOR)
	|| (patch != LIBVOXIN_VERSION_PATCH)) {
      return __LINE__;      
    }
  }
    
  ECIHand handle;
  handle = eciNew();
  if (!handle)
    return __LINE__;

  data_cb.fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (data_cb.fd == -1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
   
  eciAddText(handle," `gfa1 ");

  if (eciAddText(handle, unexpected_lower_than_character) == ECIFalse)
    return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
  return 0;
}

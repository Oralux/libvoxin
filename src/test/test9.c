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


#define PATHNAME_RAW_DATA "/tmp/test_libvoxin.raw"
#define TEST_DBG "/tmp/test_libvoxin.dbg"
#define MAX_LANGUAGES 22

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];

enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  int fd = pData - (void*)NULL;

  if (Msg == eciWaveformBuffer)
    {
      ssize_t len = write(fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}


int main()
{
  uint8_t *buf;
  size_t len;
  int res;
  ECIHand handle = eciNew();
  if (!handle)
    return __LINE__;
  
  int fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (fd==-1)
    return __LINE__;
  
  eciRegisterCallback(handle, my_client_callback, (char*)NULL + fd);
  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;

  res = eciSetParam(handle, eciInputType, 1);

  eciAddText(handle," `gfa1 ");
  eciAddText(handle," `gfa2 ");

  int i;
  const char* set_mode_fmt = "`Pf%d()?-";
  for (i=0; i<=2; i++) {
	#define MAX_STRING 256
	char set_mode[MAX_STRING];
	snprintf(set_mode, MAX_STRING, "`Pf%d()?:;!'", i);
	eciAddText(handle, set_mode);
	snprintf(set_mode, MAX_STRING, "<speak>Punctuation Mode %d</speak>", i);
	eciAddText(handle, set_mode);
	eciAddText(handle, "<speak>&amp; &lt;Bob&gt;&quot;Hello&quot; &apos;World&apos;!</speak>");
   
    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;
  }
  if (eciDelete(handle) != NULL)
	return __LINE__;
  
  return 0;
}

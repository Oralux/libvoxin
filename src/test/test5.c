/*
  2 languages, eciNew + eciSetParam
 */
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


const char *quote = "Mr Smith";



enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  int fd = pData - (void*)NULL;

  if (Msg == eciWaveformBuffer)
    {
      ssize_t len = write(fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}

int main(int argc, char **argv)
{
  int res = 0;
  uint8_t *buf;
  size_t len;

  {
    struct stat buf;
    while (!stat(TEST_DBG, &buf)) {
      sleep(1);
    }
  }

  
  ECIHand handle = eciNew();
  if (!handle)
    return __LINE__;

  int fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (fd==-1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, (char*)NULL + fd);
  eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples);
  //  eciSetOutputFilename(handle, PATHNAME_RAW_DATA);

  res = eciSetParam(handle, eciLanguageDialect, eciGeneralAmericanEnglish);
  if (res < 0)
    return __LINE__;

  res = eciGetParam(handle, eciLanguageDialect);
  if (res != eciGeneralAmericanEnglish)
    return __LINE__;

  res = eciSetVoiceParam(handle, 0, eciSpeed, 100);
  if (res < 0)
    return __LINE__;

  res = eciGetVoiceParam(handle, 0, eciSpeed);
  if (res != 100)
    return __LINE__;

  if (eciAddText(handle, quote) == ECIFalse)
    return __LINE__;

  res = eciSetParam(handle, eciLanguageDialect, eciStandardFrench);
  if (res != eciGeneralAmericanEnglish) // previous value
    return __LINE__;
  
  if (eciAddText(handle, quote) == ECIFalse)
    return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  eciReset(handle);
  if (eciAddText(handle, quote) == ECIFalse)
    return __LINE__;
  
  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
 exit0:
  return 0;
}

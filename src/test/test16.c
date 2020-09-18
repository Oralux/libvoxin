/*
  2 languages, eciNew + eciSetParam eci+nve
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "voxin.h"


#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];

const char *test[] = {
  "CAPITAL LETTER 1",
  "capital Letter 2",
  "Capital letter 3",
  "CaPital letter 4",
  "CAPITAL letter 5",
  "capital letter 6",
};

#define nveEnglishNathan 0x2d0000

void libvoxinDebugDisplayTime()
{
  static char buf[20];
  struct timeval tv;
  gettimeofday(&tv, NULL);
  snprintf(buf, 20, "%03ld.%06ld ", tv.tv_sec%1000, tv.tv_usec);
}

enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  int fd = pData - (void*)NULL;

  if (Msg == eciWaveformBuffer) {
      ssize_t len = write(fd, my_samples, 2*lParam);
      fprintf(stderr, "cb: %lu written\n", (unsigned long)2*lParam);
  }
  return eciDataProcessed;
}

int main(int argc, char **argv)
{
  int res = 0;
  uint8_t *buf;
  size_t len;
  int i;

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

  voxSetParam(handle, VOX_CAPITALS, voxCapitalSoundIcon); // sound icon
  if (eciAddText(handle, "play a sound icon before capital letters") == ECIFalse)
    return __LINE__;
  
  for (i=0; i<sizeof(test)/sizeof(*test); i++) {
    fprintf(stderr,"-> %s\n", test[i]);
    if (eciAddText(handle, test[i]) == ECIFalse)
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;
  }
  
  if (eciAddText(handle, "ignore capital letters") == ECIFalse)
    return __LINE__;
  voxSetParam(handle, VOX_CAPITALS, voxCapitalNone); // sound icon
  for (i=0; i<sizeof(test)/sizeof(*test); i++) {
    fprintf(stderr,"-> %s\n", test[i]);
    if (eciAddText(handle, test[i]) == ECIFalse)
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;
  }
  
  if (eciDelete(handle) != NULL)
    return __LINE__;
  
 exit0:
  return 0;
}

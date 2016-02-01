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

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];


const char* vh_quote = "So long as there shall exist, by virtue of law and custom, decrees of "
  "damnation pronounced by society, artificially creating hells amid the "
  "civilization of earth, and adding the element of human fate to divine "
  "destiny; so long as the three great problems of the century--the "
  "degradation of man through pauperism, the corruption of woman through "
  "hunger, the crippling of children through lack of light--are unsolved; "
  "so long as social asphyxia is possible in any part of the world;--in "
  "other words, and with a still wider significance, so long as ignorance "
  "and poverty exist on earth, books of the nature of Les Miserables cannot "
  "fail to be of use."
  " "
  "HAUTEVILLE HOUSE, 1862.";



enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  int fd = pData - (void*)NULL;

  if (Msg == eciWaveformBuffer)
    {
      ssize_t len = write(fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}

int main(int argc, char** argv)
{
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

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
  
  //  eciSetOutputFilename(handle, PATHNAME_RAW_DATA);

  if (eciAddText(handle, vh_quote) == ECIFalse)
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

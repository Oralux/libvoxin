/*
  recover from voxind crash
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
#include <string.h>
#include "eci.h"

#define PATHNAME_RAW_DATA "/tmp/test_libvoxin.raw"
#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];

#define CRASH_SAVER '|'
static const char* quote[] = {
  /* "Checking various words known to cause", */
  /* " crash in English:", */
  /* " 1. cae|sure", */
  /* " 2. vim-common-6.4.007-|4", */
  /* " 3. wed|hesday", */
  " crash in French:",
  " 1. 10 000EUR",
  /* " 2. 10000 €", */
  /* " 3. 10000 £", */
  /* " 4. 10000 $", */
  " crash in German:",
  /* " 1. dagegen", */
  /* " 2. daneben", */
};

#define NB_OF_QUOTES (sizeof(quote)/sizeof(quote[0]))


const char *test_dictionary = "test dictionary: WHO ";

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
  int i;
  char** q = malloc(sizeof(quote));
  
  {
    struct stat buf;
    while (!stat(TEST_DBG, &buf)) {
      sleep(1);
    }
  }

  for (i=0; i<NB_OF_QUOTES; i++) {    
    q[i] = strdup(quote[i]);
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

  eciSetParam(handle, eciLanguageDialect, eciStandardFrench);
  if (eciSetVoiceParam(handle, 0, eciSpeed, 80) < 0)
    return __LINE__;


  ECIDictHand hDic1 = eciNewDict(handle);
  if (!hDic1)
    return __LINE__;
  
  if (eciLoadDict(handle, hDic1, eciMainDict, "main1.dct") != DictNoError)
    return __LINE__;
  
  if (eciSetDict(handle, hDic1) != DictNoError)
    return __LINE__;

  
  for (i=0; i<NB_OF_QUOTES; i++) {
    char*s = q[i];
    int lq = strlen(s);
    int j, k;
    for (j=0,k=0; j<lq; j++) {
      if (s[j] == CRASH_SAVER)
	continue;
      else {
	s[k] = s[j];
	k++;
      }
    }
    s[k] = 0;
  }

  // check dictionary before crash
  eciAddText(handle, test_dictionary);

  for (i=0; i<NB_OF_QUOTES; i++) {
    eciAddText(handle, q[i]);    
    eciSynthesize(handle);
    eciSynchronize(handle);
  }

  // check dictionary after crash
  eciAddText(handle, test_dictionary);
  eciSynthesize(handle);
  eciSynchronize(handle);

  
  /* //  close(creat("/tmp/test_voxind",O_RDWR)); */
  
  /* eciAddText(handle, q[1]);     */
  /* eciSynthesize(handle); */
  /* eciSynchronize(handle); // crash */
  
  /* eciAddText(handle, q[2]);     */
  /* eciSynthesize(handle); */
  /* eciSynchronize(handle); // crash */

  /* if (eciSynchronize(handle) == ECIFalse) */
  /*   return __LINE__; */

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
 exit0:
  return 0;
}

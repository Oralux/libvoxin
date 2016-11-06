/*
  recover from crash
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <iconv.h>
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
#define CRASH_SAVER "|"

#define INDEX_FR 4

static const char* quote[] = {
  "0. Crash in English: ",
  "1. WHO", // main1.dct
  "2. cae" CRASH_SAVER "sure ",
  "3. WHO", // main1.dct
  "4. Crash en Français: ",
  "5. RAS", // main1-fr.dct
  "3. wilhelmina", // main1.dct
  "6. 10 000" CRASH_SAVER "EUR ",
  "7. RAS", // main1-fr.dct
};

#define NB_OF_QUOTES (sizeof(quote)/sizeof(quote[0]))


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

  if (eciSetVoiceParam(handle, 0, eciSpeed, 70) < 0)
    return __LINE__;
  
  ECIDictHand hDic1 = eciNewDict(handle);
  if (!hDic1)
    return __LINE__;

  if (eciLoadDict(handle, hDic1, eciMainDict, "main1.dct") != DictNoError)
    return __LINE__;

  if (eciSetDict(handle, hDic1) != DictNoError)
    return __LINE__;

  for (i=0; i<NB_OF_QUOTES; i++) {
    char *s = q[i];
    int lq = strlen(s);
    int j, k;
    for (j=0,k=0; j<lq; j++) {
      if (s[j] == *CRASH_SAVER)
	continue;
      else {
	s[k] = s[j];
	k++;
      }
    }
    s[k] = 0;
  }

  {
    iconv_t cd = iconv_open("ISO8859-1", "UTF8");
    for (i=INDEX_FR; i<NB_OF_QUOTES; i++) {
      size_t utf8_size = strlen(q[i]);
      size_t iso8859_1_size = 2*strlen(q[i]);
      char *iso8859_1_buf = calloc(1, iso8859_1_size);
      char *s = q[i];
      char *d = iso8859_1_buf;
      iconv(cd, &s, &utf8_size, &d, &iso8859_1_size);
      free(q[i]);
      q[i] = iso8859_1_buf;
    }
  }

  for (i=0; i<NB_OF_QUOTES; i++) {
    if (i==INDEX_FR) {
      int res;
      ECIDictHand hDic2;

      //      eciSetParam(handle, eciLanguageDialect, eciStandardFrench);

      hDic2 = eciNewDict(handle);
      if (!hDic2)
	return __LINE__;

      res = eciLoadDict(handle, hDic2, eciMainDict, "main1-fr.dct");
      if (res != DictNoError)
	return __LINE__;

      if (eciSetDict(handle, hDic2) != DictNoError)
	return __LINE__;


      if (eciLoadDict(handle, hDic2, eciRootDict, "root1.dct") != DictNoError)
      	return __LINE__;


      
    }

    eciAddText(handle, q[i]);
    eciSynthesize(handle);
    eciSynchronize(handle);
  }

  if (eciDelete(handle) != NULL)
    return __LINE__;

 exit0:
  return 0;
}

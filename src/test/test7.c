/*
  recover from crash (one engine)
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
#include <time.h>
#include <errno.h>
#include <string.h>
#include "eci.h"

enum lang_t {ENGLISH, FRENCH, MAX_LANG};
static const char *pathname[MAX_LANG] = {"/tmp/test_libvoxin_en.raw", "/tmp/test_libvoxin_fr.raw"};
static ECIHand handle[MAX_LANG];
static const enum ECILanguageDialect lang[MAX_LANG] = {eciGeneralAmericanEnglish, eciStandardFrench};

#define ONE_MILLISECOND_IN_NANOSECOND 1000000 
#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];
#define CRASH_SAVER "|"

#define INDEX_FR 5
#define INDEX_EN 9

static const char* quote[] = {
  "0. Crash in English: ",
  "1. WHO", // main1.dct
  "2. cae" CRASH_SAVER "sure ",
  "3. wilhelmina", // root1.dct
  "4. WHO", // main1.dct
  "5. Crash en Français: ",
  "6. RAS", // main1-fr.dct
  "7. 10 000" CRASH_SAVER "EUR ",
  "8. RAS", // main1-fr.dct
  "9. Crash in English: ",
  "10. WHO", // main1.dct
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


static void speak(ECIHand handle) {
  struct timespec req;
  req.tv_sec=0;
  req.tv_nsec=ONE_MILLISECOND_IN_NANOSECOND;
  
  while(eciSpeaking(handle) == ECIFalse)
    nanosleep(&req, NULL);
  
  eciPause(handle, ECITrue);
  sleep(1);
  eciPause(handle, ECIFalse);
  
  while(eciSpeaking(handle) == ECITrue)
    nanosleep(&req, NULL);  
}


int main(int argc, char** argv)
{
  uint8_t *buf;
  size_t len;
  int i;
  char** q = malloc(sizeof(quote));
  ECIHand handle[MAX_LANG];
  ECIDictHand hDic1[MAX_LANG];
  int fd;
  
  {
    struct stat buf;
    while (!stat(TEST_DBG, &buf)) {
      sleep(1);
    }
  }

  for (i=0; i<NB_OF_QUOTES; i++) {
    q[i] = strdup(quote[i]);
  }

  handle[0] = eciNew();
  if (!handle[0])
    return __LINE__;
  fd = creat(pathname[ENGLISH], S_IRUSR|S_IWUSR);
  if (fd==-1)
    return __LINE__;
  eciRegisterCallback(handle[0], my_client_callback, (char*)NULL + fd);
  if (eciSetOutputBuffer(handle[0], MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
  if (eciSetVoiceParam(handle[0], 0, eciSpeed, 70) < 0)
    return __LINE__;
  hDic1[0] = eciNewDict(handle[0]);
  if (!hDic1[0])
    return __LINE__;
  if (eciLoadDict(handle[0], hDic1[0], eciMainDict, "main1.dct") != DictNoError)
    return __LINE__;
  if (eciLoadDict(handle[0], hDic1[0], eciRootDict, "root1.dct") != DictNoError)
    return __LINE__;
  if (eciSetDict(handle[0], hDic1[0]) != DictNoError)
    return __LINE__;

  handle[1] = eciNewEx(eciStandardFrench);
  if (!handle[1])
    return __LINE__;
  if (eciSetOutputFilename(handle[1], pathname[FRENCH]) == ECIFalse)
    return __LINE__;
  if (eciSetVoiceParam(handle[1], 0, eciSpeed, 70) < 0)
    return __LINE__;
  hDic1[1] = eciNewDict(handle[1]);
  if (!hDic1[1])
    return __LINE__;
  if (eciLoadDict(handle[1], hDic1[1], eciMainDict, "main1-fr.dct") != DictNoError)
    return __LINE__;
  if (eciSetDict(handle[1], hDic1[1]) != DictNoError)
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
    for (i=INDEX_FR; i<INDEX_EN; i++) {
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
      ECIDictHand hDic1[0];
      eciSetParam(handle[0], eciLanguageDialect, eciStandardFrench);
      hDic1[0] = eciNewDict(handle[0]);
      if (!hDic1[0])
      	return __LINE__;
      if (eciLoadDict(handle[0], hDic1[0], eciMainDict, "main1-fr.dct") != DictNoError)
	return __LINE__;
      if (eciSetDict(handle[0], hDic1[0]) != DictNoError)
      	return __LINE__;
    }

    if (i==INDEX_EN) {
      eciSetParam(handle[0], eciLanguageDialect, eciGeneralAmericanEnglish);
      if (eciSetDict(handle[0], hDic1[0]) != DictNoError)
      	return __LINE__;
    }

    eciAddText(handle[0], q[i]);
    eciSynthesize(handle[0]);
    eciSynchronize(handle[0]);
  }

  for (i=INDEX_FR; i<INDEX_EN; i++) {
    eciAddText(handle[1], q[i]);
    eciSynthesize(handle[1]);
    speak(handle[1]);
  }  
  if (eciDelete(handle[1]) != NULL)
    return __LINE__;

  if (eciDelete(handle[0]) != NULL)
    return __LINE__;

 exit0:
  return 0;
}

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
  /* enum ECILanguageDialect Languages[MAX_LANGUAGES]; */
  /* int nbLanguages=MAX_LANGUAGES; */
  
  ECIHand handle = eciNewEx(eciStandardFrench);
  if (!handle)
    return __LINE__;


  int fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (fd==-1)
    return __LINE__;
  
  eciRegisterCallback(handle, my_client_callback, (char*)NULL + fd);
  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;

  res = eciSetParam(handle, 3, 0);
  res = eciSetParam(handle, 1, 1);

  eciAddText(handle," `gfa1 ");
  eciAddText(handle," `gfa2 ");
  eciAddText(handle,"`Pf0()?-");

  /* if (eciGetAvailableLanguages(Languages, &nbLanguages)) */
  /*   return __LINE__; */

  if (!eciCopyVoice(handle,1,0)) {
    char error[256];
    eciErrorMessage(handle, error);
    fprintf(stderr, "eciCopyVoice error=%s, status=0x%x\n", error, eciProgStatus(handle));
    eciClearErrors(handle);
    eciErrorMessage(handle, error);
  }
  res = eciGetVoiceParam(handle, 0, 2);
  res = eciGetVoiceParam(handle, 0, 6);
 
  /* eciErrorMessage: ENTER(0x1bcbf30,0x7ffe47801480) */
  /*    eciErrorMessage: msg=Synthes */
  if (!eciCopyVoice(handle,1,0)) {
    char error[256];
    eciErrorMessage(handle, error);
    fprintf(stderr, "eciCopyVoice error=%s, status=0x%x\n", error, eciProgStatus(handle));
    eciClearErrors(handle);
    eciErrorMessage(handle, error);
  }
  res = eciGetVoiceParam(handle, 0, 2);
  res = eciGetVoiceParam(handle, 0, 6);

  //  res = eciSetVoiceParam(handle, 0, eciSpeed, 70);
  res = eciSetVoiceParam(handle, 0, eciSpeed, 100);
  if (res < 0)
    return __LINE__;

  res = eciSetVoiceParam(handle, 0, eciVolume, 100);
  if (res < 0)
    return __LINE__;
  
  res = eciSetVoiceParam(handle, 0, eciPitchBaseline, 72);
  if (res < 0)
    return __LINE__;

  int i;
  //  for (i=0; i<=10;i++) {
  for (i=0; i<=2;i++) {
  
    eciAddText(handle,"`Pf0()?-");

    res = eciSetParam(handle, eciTextMode, 0);
    eciAddText(handle,"<speak>Screen reader on.</speak>");

    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

    eciAddText(handle,"`Pf2()?-");
    res = eciSetParam(handle, 2, 0);
    eciAddText(handle,"<speak>Desktop frame</speak>");
    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

    res = eciSetParam(handle, 2, 0);
    eciAddText(handle,"<speak>page tab list</speak>");
    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

    res = eciSetParam(handle, 2, 0);
    eciAddText(handle,"<speak>ESSAYEZ-LE POUR 5 JOURS heading level 2</speak>");
    
    if (!eciInsertIndex(handle,0))
      return __LINE__;
    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

    usleep(1400000);
    if (eciStop(handle) == ECIFalse) {
      char error[256];
      eciErrorMessage(handle, error);
      fprintf(stderr, "eciStop error=%s, status=0x%x\n", error, eciProgStatus(handle));
      eciClearErrors(handle);
      eciErrorMessage(handle, error);
    }
  }

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
  return 0;
}

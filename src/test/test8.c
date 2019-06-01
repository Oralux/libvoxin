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
  
  //  ECIHand handle = eciNew();
  ECIHand handle = eciNewEx(eciStandardFrench);
  if (!handle)
    return __LINE__;
  
  int fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (fd==-1)
    return __LINE__;
  
  eciRegisterCallback(handle, my_client_callback, (char*)NULL + fd);
  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;

  res = eciSetParam(handle, eciDictionary, 0);
  res = eciSetParam(handle, eciInputType, 1);

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

  res = eciSetVoiceParam(handle, 0, eciSpeed, 70);
  if (res < 0)
    return __LINE__;

  res = eciSetVoiceParam(handle, 0, eciVolume, 100);
  if (res < 0)
    return __LINE__;
  
  res = eciSetVoiceParam(handle, 0, eciPitchBaseline, 72);
  if (res < 0)
    return __LINE__;

  typedef enum {
	eciTextModeDefault = 0,
	eciTextModeAlphaSpell = 1,
	eciTextModeAllSpell = 2,
	eciIRCSpell = 3
  } ECITextMode;

  ECITextMode mode = eciTextModeDefault;
  int i;
  for (i=0; i<2;i++) {
	// PUNC_NONE
    eciAddText(handle,"`Pf0()?-");

	mode = (!i) ? eciTextModeDefault : eciTextModeAlphaSpell; 
    res = eciSetParam(handle, eciTextMode, mode);
	//    eciAddText(handle,"<speak>Test punctuation: (0).</speak>");

	eciAddText(handle,"<speak>Test de ponctuation.</speak>");
    eciAddText(handle,"<speak>Étape : (0).</speak>");
	// <speak> + punc + 2b (È = 2 bytes in UTF8)
	eciAddText(handle,"<speak>PREMIÈRE PROMENADE. Me voici donc seul ");
	// </speak> punc + 2b + 3b (’ = 3 bytes in UTF-8)
	eciAddText(handle,"sur la terre, n’ayant plus de frère, de prochain, d'ami,<speak>");
	// <speak> + 2b
	eciAddText(handle,"<speak>de société");
	// 2b
	eciAddText(handle,"que moi-même Le plus sociable et le plus aimant");
	// 2b + punc
	eciAddText(handle,"des humains en a été proscrit.");
	// </speak>
	eciAddText(handle," </speak>");
	// <speak>
	eciAddText(handle,"<speak>");
	// space
	eciAddText(handle," ");
	// punc
	eciAddText(handle,".");
	eciAddText(handle," </speak>");
	// speak + punc
	eciAddText(handle,"<speak>.");
	// </speak> + 2b
	eciAddText(handle,"Par un accord unanime ils ont cherché dans les raffinements</speak>");
	eciAddText(handle,"<speak>de leur haine quel tourment pouvait être le plus cruel à mon âme");
	// </speak> + punc
	eciAddText(handle,"sensible,</speak>");
   
    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

	// PUNC_ALL
    eciAddText(handle,"`Pf1()?-");
	mode = (!i) ? eciTextModeDefault : eciTextModeAllSpell; 
    res = eciSetParam(handle, eciTextMode, mode);
    eciAddText(handle,"<speak>Étape : (1).</speak>");
    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

	// PUNC_SOME
	mode = (!i) ? eciTextModeDefault : eciTextModeAllSpell; 
    res = eciSetParam(handle, eciTextMode, mode);
    eciAddText(handle,"`Pf2()?-");
    eciAddText(handle,"<speak>Étape : (2).</speak>");
    if (!eciInsertIndex(handle,0))
      return __LINE__;

    if (eciSynthesize(handle) == ECIFalse)
      return __LINE__;

    if (eciSynchronize(handle) == ECIFalse)
      return __LINE__;

	mode = (!i) ? eciTextModeDefault : eciTextModeAllSpell; 
    res = eciSetParam(handle, eciTextMode, mode);
    eciAddText(handle,"<speak>FIN-ÉTAPES.</speak>");
	//    eciAddText(handle,"<speak>END-OF-TEST</speak>");
    
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

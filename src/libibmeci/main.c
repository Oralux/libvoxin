#include <stdlib.h>
#include <stdint.h>
/* #include <string.h> */
/* #include <errno.h> */
/* #include <stdbool.h> */
/* #include <pthread.h> */
#include "voxin.h"
#include "debug.h"
/* #include "conf.h" */
/* #include "libvoxin.h" */
/* #include "msg.h" */
/* #include <unistd.h> */


ECIHand eciNew(void)
{
  ENTER();
  return (ECIHand)0x1234;
}

Boolean eciSetOutputBuffer(ECIHand hEngine, int iSize, short *psBuffer)
{ 
  ENTER();
  return ECITrue;
}

Boolean eciSetOutputFilename(ECIHand hEngine, const void *pFilename)
{
  ENTER();
  return ECITrue;
}

Boolean eciAddText(ECIHand hEngine, ECIInputText pText)
{
  ENTER();
  return ECITrue;
}

Boolean eciSynthesize(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}

Boolean eciSynchronize(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}


ECIHand eciDelete(ECIHand hEngine)
{
  ENTER();
  return NULL_ECI_HAND;
}

void eciRegisterCallback(ECIHand hEngine, ECICallback Callback, void *pData)
{
  ENTER();
}

Boolean eciSpeaking(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}


Boolean eciStop(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}

int eciGetAvailableLanguages(enum ECILanguageDialect *aLanguages, int *nLanguages)
{
  ENTER();
  return 0;
}

ECIHand eciNewEx(enum ECILanguageDialect Value)
{
  ENTER();
  return (ECIHand)0x1234;
}

int eciSetParam(ECIHand hEngine, enum ECIParam Param, int iValue)
{
  ENTER();
  return 0;
}

int eciSetDefaultParam(enum ECIParam parameter, int value)
{
  ENTER();
  return 0;
}

int eciGetParam(ECIHand hEngine, enum ECIParam Param)
{
  ENTER();
  return 0;  
}

int eciGetDefaultParam(enum ECIParam parameter)
{
  ENTER();
  return 0;  
}

void eciErrorMessage(ECIHand hEngine, void *buffer)
{
  ENTER();
}

int eciProgStatus(ECIHand hEngine)
{
  ENTER();
  return 0;
}
  
void eciClearErrors(ECIHand hEngine)
{
  ENTER();
}

Boolean eciReset(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}

void eciVersion(char *pBuffer)
{
  ENTER();

  if (pBuffer) {
	*(char*)pBuffer=0;
  }
}

int eciGetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param)
{
  ENTER();
  return 0;
}

int eciSetVoiceParam(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param, int iValue)
{
  ENTER();
  return 0;
}

Boolean eciPause(ECIHand hEngine, Boolean On)
{
  ENTER();
  return ECITrue;
}

Boolean eciInsertIndex(ECIHand hEngine, int iIndex)
{
  ENTER();
  return ECITrue;
}

Boolean eciCopyVoice(ECIHand hEngine, int iVoiceFrom, int iVoiceTo)
{
  ENTER();
  return ECITrue;
}

ECIDictHand eciNewDict(ECIHand hEngine)
{
  ENTER();
  return (ECIDictHand)0x5678;
}

ECIDictHand eciGetDict(ECIHand hEngine)
{
  ENTER();
  return (ECIDictHand)0x5678;
}

enum ECIDictError eciSetDict(ECIHand hEngine, ECIDictHand hDict)
{
  ENTER();
  return 0;  
}  

ECIDictHand eciDeleteDict(ECIHand hEngine, ECIDictHand hDict)
{
  ENTER();
  return NULL_DICT_HAND;  
}

enum ECIDictError eciLoadDict(ECIHand hEngine, ECIDictHand hDict, enum ECIDictVolume DictVol, ECIInputText pFilename)
{
  ENTER();
  return DictFileNotFound;
}

Boolean eciClearInput(ECIHand hEngine)
{
  ENTER();
  return ECITrue;
}

Boolean ECIFNDECLARE eciSetOutputDevice(ECIHand hEngine, int iDevNum)
{
  ENTER();
  return ECITrue;
}


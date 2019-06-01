/*
  Main dictionary
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
#include <linux/limits.h>
#include "eci.h"


#define TEST_DBG "/tmp/test_libvoxin.dbg"

//#define MAX_SAMPLES 20000
#define MAX_SAMPLES 1024
static short my_samples[MAX_SAMPLES];


const char *quote = "ibmtts "
  "WHO "
  "AWSA "
  "jeb@notreal.org ";
  /* "ECSU " */
  /* "UConn " */
  /* "WYSIWYG " */
  /* "Win32 " */
  /* "486DX "; */



enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  int fd = pData - (void*)NULL;

  if (Msg == eciWaveformBuffer)
    {
      ssize_t len = write(fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}

// Retrieve the absolute pathname of the dictionary.  In this test,
// the dictionary is supposed to be in the current working directory
static char *absolutePath(ECIHand handle, char *filename)
{
#define PATH_DICT (2*PATH_MAX)
	static char path[PATH_DICT];
	*path = 0;
	if (!getcwd(path, PATH_DICT))
	  return NULL;
	strncat(path, "/", 2*PATH_MAX);
	strncat(path, filename, 2*PATH_MAX);
	path[PATH_DICT-1] = 0;
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

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;

  ECIDictHand hDic1 = eciNewDict(handle);
  if (!hDic1)
    return __LINE__;

  if (eciLoadDict(handle, hDic1, eciMainDict, absolutePath(handle, "main1.dct")) != DictNoError)
	return __LINE__;
  
  if (eciSetDict(handle, hDic1) != DictNoError)
    return __LINE__;

  hDic1 = eciGetDict(handle);
  if (!hDic1)
    return __LINE__;
  
  if (eciAddText(handle, quote) == ECIFalse)
    return __LINE__;

  if (eciSynthesize(handle) == ECIFalse)
    return __LINE__;

  if (eciSynchronize(handle) == ECIFalse)
    return __LINE__;

  if (eciDeleteDict(handle, hDic1) != NULL)
    return __LINE__;
    
  
  /* if (eciAddText(handle, quote) == ECIFalse) */
  /*   return __LINE__; */
  
  /* if (eciSynthesize(handle) == ECIFalse) */
  /*   return __LINE__; */

  /* if (eciSynchronize(handle) == ECIFalse) */
  /*   return __LINE__; */

  if (eciDelete(handle) != NULL)
    return __LINE__;
  
 exit0:
  return 0;
}

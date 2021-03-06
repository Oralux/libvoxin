/*
  eciSynchronize
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

const char *text[] = {
  "01u[éçïmà]\n",
  "01l[" "\xe9\xe7\xef\x6d\xe0" "]\n",
  "02[" "\xec" "ABC]\n",
  "03[A" "\xec" "BC]\n",
  "04[AB" "\xec" "C]\n",
  "05[" "\x02" "ABC]\n",
  "06[A" "\x02" "BC]\n",
  "07[AB" "\x02" "C]\n",
};
#define MAX_TEXT (sizeof(text)/sizeof(*text))
typedef struct {
  int fd;
} data_cb_t;

static data_cb_t data_cb;

enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  data_cb_t *data_cb = (data_cb_t *)pData;

  if (data_cb && (Msg == eciWaveformBuffer))
    {
      ssize_t len = write(data_cb->fd, my_samples, 2*lParam);
    }
  return eciDataProcessed;
}

int main(int argc, char** argv)
{
  uint8_t *buf;
  size_t len;
  int i;
  
  {
    struct stat buf;
    while (!stat(TEST_DBG, &buf)) {
      sleep(1);
    }
  }

  
  ECIHand handle = eciNewEx(eciStandardFrench);
  if (!handle)
    return __LINE__;

  data_cb.fd = creat(PATHNAME_RAW_DATA, S_IRUSR|S_IWUSR);
  if (data_cb.fd == -1)
    return __LINE__;

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (eciSetOutputBuffer(handle, MAX_SAMPLES, my_samples) == ECIFalse)
    return __LINE__;
  
  //  eciSetOutputFilename(handle, PATHNAME_RAW_DATA);
  //  eciAddText(handle," `gfa1 ");

  for (i=0; i<MAX_TEXT; i++) { 
  if (eciAddText(handle, text[i]) == ECIFalse)
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

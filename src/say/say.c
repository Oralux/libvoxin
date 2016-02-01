/* This file is under the LGPL license */

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <eci.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "debug.h"

#define BUFFERSIZE (100*1024)
static short buffer[BUFFERSIZE];

void usage()
{
  fprintf(stderr, "Usage: say [-f filename][-w wavfile] \n");
}


static enum ECICallbackReturn wav_callback(ECIHand eciHand, enum ECIMessage msg, long lparam, void *data)
{
  int fd = (char*)data - (char*)NULL;
  ENTER();
  //  printf("data=%x, lparam=%d, msg=%d, fd=%x\n",data, lparam, msg, fd);

  if (msg == eciWaveformBuffer)
  {
    ssize_t len = write(fd, buffer, 2*lparam);
    if (len != 2*lparam) {
      err("len=%ld",len);
    } else {
      dbg("len=%ld",len);
    }
  }
  return eciDataProcessed;
}

static void say_text(char *text)
{

  ECIHand aHandle;

  ENTER();
 
  aHandle = eciNew();

  if (aHandle == NULL_ECI_HAND)
    {
      printf( "Error eciNew\n" );
      return;
    }
  if ((eciSetParam (aHandle, eciInputType, 1) == -1)
	   || (eciSetParam (aHandle, eciSynthMode, 1) == -1)
	   || (eciSetParam (aHandle, eciSampleRate, 1) == -1))
    {
      printf( "Error eciSetParam\n" );
      return;
    }
  if (!eciAddText(aHandle, text))
    {
      printf( "Error eciAddText\n" );
      return;
    }
  if (!eciSynthesize(aHandle))
    {
      printf( "Error eciSynthesize\n" );
      return;
    }

  {
    int i=0;
    while(i<10) {
      if (eciSpeaking(aHandle)) {
	dbg("speaking");
      }
      sleep(1);
      ++i;
    }
  }
  if (!eciSynchronize(aHandle))
    {
      printf( "Error eciSynchronize\n" );
      return;
    }
  eciDelete(aHandle);
}

static void convert_text_to_wav(char *text, char *wavfile)
{
  ECIHand eciHand = eciNew();
  ENTER();

  int fd = creat(wavfile, S_IRUSR|S_IWUSR);
  if (fd==-1) {
    err("errno=%d", errno);
    return;
  }

  eciRegisterCallback(eciHand, wav_callback, (char*)NULL + fd);
  eciSetOutputBuffer(eciHand, BUFFERSIZE, buffer);

  //  eciSetOutputFilename(eciHand, filename);

  eciAddText(eciHand, text);
  
  //  printf("addtext=%s, buffer=0x%x\n",text, buffer);
  
  eciSynthesize(eciHand);
  {
    int i=0;
    while(i<1000) {
      if (eciSpeaking(eciHand)) {
	dbg("speaking");
      }
      ++i;
    }
  }
  //Wait until synthesis is complete
  eciSynchronize (eciHand);
  eciDelete(eciHand);
}



int main(int argc, char *argv[]) {
  char *wavfile = NULL;
  char *sentence = NULL;
  int opt;

  ENTER();

  while ((opt = getopt(argc, argv, "w:f:")) != -1) {
    switch (opt) {
    case 'w':
      wavfile = strdup(optarg);
      break;
      
    case 'f':
      {
	struct stat buf;
	int fd;
	size_t len;
	char *pathname = strdup(optarg);

	if (stat(pathname, &buf) == -1) {
	  usage();
	  exit(errno);
	}

	fd = open(pathname,O_RDONLY);
	if (fd == -1){
	  usage();
	  exit(errno);
	}
	sentence = (char*)calloc(1, buf.st_size+1);
	len = read(fd, sentence, buf.st_size);
	sentence[len]=0;
	free(pathname);
      }
      break;

    default:
      usage();
      exit(1);
    }
  }

  if (optind == argc-1) {
    sentence = strdup(argv[optind]);
  } else if (!sentence) {
    sentence = strdup("Hello World!");
  }

  if (wavfile == NULL) {
    say_text(sentence);
  } else {
    convert_text_to_wav(sentence, wavfile);    
  }
  
  
  return 0;
}



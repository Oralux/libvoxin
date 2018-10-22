/* This file is under the LGPL license */

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <eci.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "debug.h"

#define MAX_SAMPLES 10240
#define MAX_CHAR 10240
#define MAX_JOBS 32
static char sentences[MAX_CHAR+10];

void usage()
{
  fprintf(stderr, "Usage: say -w wavfile [-f textfile] [-j jobs] \"optional text\"\n \
 \n\
say (version %s) \n\
Converts the text to a wav file. \n\
The text can be supplied in a file or as the last argument of the \n\
command line (between quotes). \n\
\n\
OPTIONS :\n\
  wavfile    the output wavfile (without header by default)	\n\
  textfile   (optional) text file to be spoken. \n\
  jobs       (optional) number of jobs, share the worload on several \n\
             processes to speedup the overall conversion. \n\
", VERSION);
}

typedef struct {
  int fd;
} data_cb_t;

static data_cb_t data_cb;


typedef struct {
  char *filename;
  FILE *fd;
} file_t;

typedef struct {
  long begin;
  long end;
} region_t;

typedef struct {
  void *handle;
  short samples[MAX_SAMPLES];
} tts_t;

typedef struct {
  file_t text;
  region_t region;
  file_t wav[MAX_JOBS];
  int with_wav_header;
  tts_t tts;
  int jobs;
} obj_t;

static obj_t obj;

typedef struct __attribute__((__packed__)) {
  char chunkID[4];
  uint32_t chunkSize;
  char format[4];
  char subChunk1ID[4];
  uint32_t subChunk1Size;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char subChunk2ID[4];
  uint32_t subChunk2Size;
} wav_header_t;


static void setWavHeader(wav_header_t *w, uint32_t wavSize)
{
  uint32_t rawSize = 0;

  if (!w)
	return;

  if (wavSize < sizeof(wav_header_t))
	wavSize = sizeof(wav_header_t);

  rawSize = wavSize - sizeof(wav_header_t);
  
  // code expected to run on little endian arch
  strcpy(w->chunkID, "RIFF");
  w->chunkSize = wavSize - 8;
  strcpy(w->format, "WAVE");
  strcpy(w->subChunk1ID, "fmt ");

  w->subChunk1Size = 16; // pcm
  w->audioFormat = 1; // pcm
  w->numChannels = 1;
  w->sampleRate = 11025;
  w->bitsPerSample = 16;
  w->byteRate = w->sampleRate*w->numChannels*(w->bitsPerSample/8);

  w->blockAlign = w->numChannels*(w->bitsPerSample/8);
  strcpy(w->subChunk2ID, "data");
  w->subChunk2Size = rawSize;
 
  
/* 00000000  52 49 46 46 78 22 10 00  57 41 56 45 66 6d 74 20  |RIFFx"..WAVEfmt | */
/* 00000010  10 00 00 00 01 00 01 00  11 2b 00 00 22 56 00 00  |.........+.."V..| */
/* 00000020  02 00 10 00 64 61 74 61  54 22 10 00 85 ff ea ff  |....dataT"......| */

/* <4:"RIFF">     <4:riffsize>             <8:"WAVEfmt "> */
/* <4:0x00000010> <2:0x0001> <2:0x0001>    <4:0x00002b11> <4:00005622> */
/* <2:0x0002> <2:0x0010> <4:"data"> <4:dataSize> */
  
}

static int createWAV(FILE *fd)
{
  wav_header_t w; 
  if (!fd)
	return -1;
  memset(&w, 0, sizeof(w));
  fwrite(&w, 1, sizeof(w), fd);
  return 0;
}

static int updateHeaderWAV()
{
  wav_header_t w;
  long wavSize = 0;
  struct stat statbuf;
  int i;
  int err = 0;

  for (i=0; i<obj.jobs; i++) {
	if (!obj.wav[i].filename) {
	  err = EINVAL;
	  goto exit0;	  
	}
	if (stat(obj.wav[i].filename, &statbuf)) {
	  err = errno;
	  goto exit0;	  
	}
	wavSize += statbuf.st_size;
  }

  setWavHeader(&w, wavSize);

  if (!obj.wav[0].fd) {
	obj.wav[0].fd = fopen(obj.wav[0].filename, "r");
	if (!obj.wav[0].fd) {
	  err = errno;
	  goto exit0;	  
	}
  } else {
	rewind(obj.wav[0].fd);
  }
  
  fwrite(&w, 1, sizeof(w), obj.wav[0].fd);

 exit0:
  if (err) {
	err("%s", strerror(err));
  }  
  return err;
}

static int closeWAV()
{
  int i;
  for (i=0; i<obj.jobs; i++) {
	if (obj.wav[i].fd) {
	  fclose(obj.wav[i].fd);
	  obj.wav[i].fd = NULL;
	}
  }
  return 0;
}

static int mergeWAV(const char *filename)
{
  int err = 0;
  int i;  
  FILE *fd = NULL;

  if (!filename) {
	err = EINVAL;
	goto exit0;
  }

  fd = fopen(filename, "w");
  if (!fd) {
	err = errno;
	goto exit0;
  }

  for (i=0; i < obj.jobs; i++) {
	FILE *fdi = fopen(obj.wav[i].filename, "r");
	int size = 1;
	while(size) {
	  size = fread(sentences, 1, MAX_CHAR, fdi);
	  fwrite(sentences, 1, size, fd);
	}
	fclose(fdi);
	unlink(obj.wav[i].filename);
	free(obj.wav[i].filename);
	obj.wav[i].filename = NULL;
  }
	
 exit0:
  if (fd) {
	fclose(fd);
  }
  if (err) {
	err("%s", strerror(err));
  }  
  return err;
}

static enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  data_cb_t *data_cb = (data_cb_t *)pData;

  if (data_cb && (Msg == eciWaveformBuffer)) {
	ssize_t len = write(data_cb->fd, obj.tts.samples, 2*lParam);
  }
  return eciDataProcessed;
}

static ECIHand initECI()
{
  ENTER();
  int err = 0;
  ECIHand handle;
  
  if (!obj.wav[0].filename)
	return NULL;

  if (obj.wav[0].fd)
	fclose(obj.wav[0].fd);
  
  obj.wav[0].fd = fopen(obj.wav[0].filename, "w");
  if (!obj.wav[0].fd)
	return NULL;
  if (obj.with_wav_header) {
	createWAV(obj.wav[0].fd);
  }
  
  handle = eciNew();
  if (!handle)
	return NULL;

  data_cb.fd = fileno(obj.wav[0].fd);
  if (data_cb.fd == -1) {
	err = errno;
	goto exit0;
  }

  eciRegisterCallback(handle, my_client_callback, &data_cb);

  if (!eciSetOutputBuffer(handle, MAX_SAMPLES, obj.tts.samples))
	goto exit0;

  return handle;
  
 exit0:
  if (handle) {
	eciDelete(handle);
  }
  if (err) {
	err("%s", strerror(err));
  }
  return NULL;
}


static int saySentences()
{
  int err = EINVAL;
  ENTER();
  msg("[%d] ENTER %s", getpid(), __func__);

  if (!obj.tts.handle) {
	obj.tts.handle = initECI(obj.wav[0].filename);
	if (!obj.tts.handle) {
	  goto exit0;
	}
  }

  if (!eciAddText(obj.tts.handle, sentences)) {
	sentences[16] = 0;
	err("Error: add text=%s...", sentences);
  } else if (!eciSynthesize(obj.tts.handle)) {
	err("Error: synth handle=0x%p", obj.tts.handle);
  } else if (!eciSynchronize(obj.tts.handle)) {
	err("Error: sync handle=0x%p", obj.tts.handle);
  } else {
	err = 0;
  }

  err = updateHeaderWAV();
  if (err) {
	goto exit0;
  }
  err = closeWAV();

exit0:  
  return err;
}

static void searchLastSentence(long *length)
{
  if (!length)
	return;
  
  if (*length > 2) {
	long length0 = *length;
	int i;
	int found=0;
	for (i=*length-2; i>0; i--) {
	  if ((sentences[i] == '.') && isspace(sentences[i+1])) {
		found = 1;
		length0 = i+2;
		break;
	  }
	}
	if (!found) {
	  for (i=*length-2; i>0; i--) {
		if (!isspace(sentences[i]) && isspace(sentences[i+1])) {
		  length0 = i+2;
		  break;
		}
	  }
	}
	*length = length0;
  }
}

static int getPosPreviousSentence()
{
  int max = 0;
  long length = 0;
  long length0 = 0;
  long rel = 0;
  long offset = obj.region.end - obj.region.begin;
  
  if (!obj.text.fd || (offset <= 0)) {
	return EINVAL;
  }
  
  max = (offset < MAX_CHAR) ? offset : MAX_CHAR;
  
  if (fseek(obj.text.fd, max - offset, SEEK_END) == -1) {
	int err = errno;
	err("%s",strerror(err));
	return err;
  }

  length = fread(sentences, 1, max, obj.text.fd);
  length0 = length;
  searchLastSentence(&length0);
  offset -= (length - length0);
  return 0;
}

static int getSentences(long *length)
{
  int err = 0;
  long max, x;

  msg("[%d] ENTER %s", getpid(), __func__);
  
  if (!length || !obj.text.fd) {
	err = EINVAL;
	goto exit0;
  }

  *length = 0;
  *sentences = 0;
  
  if (obj.region.end <= obj.region.begin) {
	goto exit0;
  }
  x = obj.region.end - obj.region.begin;
  max = (x < MAX_CHAR) ? x : MAX_CHAR;  

  if (fseek(obj.text.fd, obj.region.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }
  *length = fread(sentences, 1, max, obj.text.fd);
  searchLastSentence(length);
  sentences[*length] = 0;
  msg("[%d] read from=%ld, to=%ld [%s] ", getpid(), obj.region.begin, obj.region.end, (obj.wav[0].filename) ? obj.wav[0].filename : "null");

 exit0:
  if (err) {
	err("%s",strerror(err));
  }
  return err;
}

static int sayText()
{
  long length = 0;
  int err = 0;
  if (!obj.text.fd)
	return EINVAL;

  msg("[%d] begin=%ld, end=%ld [%s] ", getpid(), obj.region.begin, obj.region.end, (obj.wav[0].filename) ? obj.wav[0].filename : "null");
  if (fseek(obj.text.fd, obj.region.begin, SEEK_SET) == -1) {
	int err = errno;
	err("%s",strerror(err));
	return err;
  }
  
  length = 1;
  while(length) {
	err = getSentences(&length);
	if (err)
	  break;
	
  	if (!*sentences) {
  	  if (feof(obj.text.fd)) {
  		err=0;
  		break;
  	  } else if (ferror(obj.text.fd)) {
  		err("file error: %s", obj.text.filename);
		err = EIO;
  		break;
  	  }
  	} else {
	  saySentences();
	}
	obj.region.begin += length;
  }
  return err;
}


static int jobs()
{
	long partlen = 0;
	struct stat statbuf;
	pid_t pid[MAX_JOBS];
	char *wavFilename = obj.wav[0].filename;
	int i = 0;
	int err = 0;
	
	if (!obj.text.filename || !obj.jobs || (obj.jobs > MAX_JOBS) || !obj.wav[0].filename) {
	  err = EINVAL;
	  goto exit0;
	}
	
	if (stat(obj.text.filename, &statbuf)) {
	  err = errno;
	  goto exit0;
	}

	if (statbuf.st_size < obj.jobs) {
	  obj.jobs = 1;
	}
	  
	partlen = statbuf.st_size/obj.jobs;
	obj.region.begin = 0;
	for (i=0; i<obj.jobs; i++) {
	  const char* fmt="%s.part%d.raw";
	  obj.region.end = (i == obj.jobs-1) ? statbuf.st_size : obj.region.begin + partlen;
	  if (getPosPreviousSentence() == -1) {
		err = EINVAL;
		goto exit0;
	  }
		
	  obj.wav[i].filename = (char*)malloc(strlen(wavFilename)+strlen(fmt)+10);
	  if (!obj.wav[i].filename) {
		err = errno;
		goto exit0;
	  }
	  sprintf(obj.wav[i].filename, fmt, wavFilename, i);
	  pid[i] = fork();
	  if (!pid[i]) {
		obj.jobs = 1;
		obj.wav[0].filename = obj.wav[i].filename;
		if (obj.wav[0].fd) {
		  fclose(obj.wav[0].fd);
		  obj.wav[0].fd = NULL;
		}
		obj.with_wav_header = (!i) ? 1 : 0;
		if (obj.text.fd) {
		  fclose(obj.text.fd);
		}
		obj.text.fd = fopen(obj.text.filename, "r");
		err = sayText();
		exit(err);
	  }
	  msg("[%d] child pid=%d, begin=%ld, end=%ld [%s] ", getpid(), pid[i], obj.region.begin, obj.region.end, (obj.wav[i].filename) ? obj.wav[i].filename : "null");
	  
	  obj.region.begin = obj.region.end;
	}
	
	for (i=0; i < obj.jobs; i++) {
	  int status;
	  int pid = wait(&status);
	  err = EINTR;
	  if (WIFEXITED(status)) {
		err = WEXITSTATUS(status);
	  }
	  if (err) {
		goto exit0;
	  }
	}
	
	err = updateHeaderWAV();
	if (err) {
	  goto exit0;
	}
	err = closeWAV();
	if (err) {
	  goto exit0;
	}
	err = mergeWAV(wavFilename);
	
 exit0:
	if (wavFilename)
	  free(wavFilename);
	return err;
}


int main(int argc, char *argv[])
{
  int help = 0;
  int opt;
  int err = EINVAL;
  
  ENTER();

  *sentences = 0;
  obj.jobs = 1;	  

  while ((opt = getopt(argc, argv, "hf:j:w:")) != -1) {
    switch (opt) {
    case 'w':
	  if (obj.wav[0].filename)
		free(obj.wav[0].filename);
      obj.wav[0].filename = strdup(optarg);
      break;
      
    case 'f':
	  if (obj.text.filename)
		free(obj.text.filename);
	  obj.text.filename = strdup(optarg);	  
      break;

    case 'j':
	  obj.jobs = atoi(optarg);	  
      break;

    case 'h':
	  help = 1;	  
      break;

    default:
      err = EINVAL;
	  goto exit0;
    }
  }

  if (help || (obj.wav[0].filename == NULL)) {
	usage();
	goto exit0;
  }
  obj.with_wav_header = 1;

  if (optind == argc-1) {
	strncpy(sentences, argv[optind], MAX_CHAR);	  
	sentences[MAX_CHAR] = 0;
  }
  
  if (obj.text.filename) {
	struct stat statbuf;	
	obj.text.fd = fopen(obj.text.filename, "r");
	if (!obj.text.fd) {
	  err = errno;
	  goto exit0;
	}
	if (stat(obj.text.filename, &statbuf)) {
	  err = errno;
	  goto exit0;	  
	}
	obj.region.end = statbuf.st_size;
  }

  if ((obj.jobs <= 0) || (obj.jobs > MAX_JOBS)) {
	err = EINVAL;
	err("jobs=%d (limit=1..%d)", obj.jobs, MAX_JOBS);
	goto exit0;
  } else if (obj.jobs != 1) {
	err = jobs();
	goto exit0;
  }

  if (obj.text.filename) {
	return sayText();
  }

  if (!*sentences) {
	strcpy(sentences, "Hello World!");
  }
  err = saySentences();
  
 exit0:
  if (obj.text.fd) {
	fclose(obj.text.fd);
  }
  if (obj.wav[0].fd) {
	fclose(obj.wav[0].fd);
  }
  if (obj.tts.handle) {
	eciDelete(obj.tts.handle);
  }

  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }
  
  return 0;
}


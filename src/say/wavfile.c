#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "file.h"
#include "textfile.h"
#include "wavfile.h"
#include "errno.h"
#include "debug.h"

#define FILE_TEMPLATE "/tmp/voxin-say.XXXXXXXXXX"
#define FILE_TEMPLATE_LENGTH 30

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

typedef struct {
  file_t **part;
  size_t number_of_parts; // number of elements in the part array
  wav_header_t header;
  file_t *output;
} wavfile_t;

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

static int updateHeader(wavfile_t *self, uint32_t wavSize) {
  uint32_t rawSize = 0;
  int err = 0;
  wav_header_t *w;
  
  if (!self) {
	return EINVAL;
  }
  
  w = &self->header;
  if (wavSize < sizeof(*w)) {
	wavSize = sizeof(*w);
  }

  rawSize = wavSize - sizeof(*w);

  // code expected to run on little endian arch
  strcpy(w->chunkID, "RIFF");
  w->chunkSize = wavSize - 8;
  strcpy(w->format, "WAVE");
  strcpy(w->subChunk1ID, "fmt ");

  w->subChunk1Size = 16; // pcm
  w->audioFormat = 1; // pcm
  w->numChannels = 1;
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

  return 0;  
}

int wavfileDelete(void *handle) {
  ENTER();
  
  wavfile_t *self = handle;
  
  if(!self)
	return 0;

  if (self->output) {
	fileDelete(self->output);
	self->output = NULL;
  }
  if (self->part) {
	int i;
	for (i=0; i<self->number_of_parts; i++) {
	  fileDelete(self->part[i]);
	  self->part[i] = NULL;
	}
  }
  self->number_of_parts = 0;
  free(self);
  
  return 0;
}

void *wavfileCreate(const char *output, size_t number_of_parts, uint32_t rate) {
  ENTER();

  wavfile_t *self = calloc(1, sizeof(*self));
  int i;
  if (!self)
	goto exit0;
  
  if (number_of_parts < 1)
	goto exit0;

  self->header.sampleRate = rate;
  
  { // check output
	struct stat statbuf;
	if (output) {
	  FILE *fdo = fopen(output, "w");
	  if (fdo)  {
		fclose(fdo);
		self->output = fileCreate(output, true, false);
	  }
	} else if (fstat(STDOUT_FILENO, &statbuf)) {
	  // no action
	} else if (S_ISREG(statbuf.st_mode)) {
	  output = realpath("/proc/self/fd/1", NULL);
	  if (output) {
		self->output = fileCreate(output, true, false);
	  }
	} else if (S_ISFIFO(statbuf.st_mode)) {
	  self->output = fileCreate(NULL, true, true);
	}
  }

  if (!self->output)
	goto exit0;
  
  self->number_of_parts = number_of_parts;
  self->part = calloc(number_of_parts, number_of_parts*(sizeof(*self->part)));
  if(!self->part)
	goto exit0;

  for (i=0; i<number_of_parts; i++) {
	self->part[i] = fileCreate(NULL, true, false);
	if (!self->part[i])
	  goto exit0;
  }
  
  return self;

 exit0:
  wavfileDelete(self);
  return NULL;
}

int wavfileWriteData(void *handle, unsigned int part, uint8_t *data, size_t len) {
  ENTER();
  
  wavfile_t *self = handle;
  int err = 0;
  
  if(!self || (part > self->number_of_parts))
	return EINVAL;

  if (fileWrite(self->part[part], data, len)) {
	err = EIO;
  }

  return err;
}


int wavfileFlush(void *handle) {
  wavfile_t *self = handle;
  size_t size = 0;
  int i;
  file_t *f;

  if(!self)
	return EINVAL;
  
  for (i=0; i<self->number_of_parts; i++) {
	size += self->part[i]->len;
  }

  updateHeader(self, (uint32_t)size);

  if (fileWrite(self->output, (uint8_t *)&self->header, sizeof(self->header))) {
	return EIO;	
  }
  
  for (i=0; i<self->number_of_parts; i++) {
	if (fileAppend(self->output, self->part[i])) {
	  return EIO;	
	}
  }

}



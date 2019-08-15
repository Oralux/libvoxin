#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "textfile.h"
#include "wavfile.h"
#include "errno.h"
#include "debug.h"

#define FILE_TEMPLATE "/tmp/voxin-say.XXXXXXXXXX"
#define FILE_TEMPLATE_LENGTH 30

typedef struct {
  char *filename;
  FILE *fd;
} file_t;

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
  file_t *file;
  bool withHeader;
  wav_header_t header;
} wavfile_t;

typedef struct {
  file_t *file;
  bool withHeader;
  wav_header_t header;
  region_t *region;
} textfile_t;

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

static file_t *fileCreate(const char *filename, bool writable) {
  file_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;

  if (filename) {
	self->filename = strdup(filename);
	if (!self->filename) {
	  free(self);
	  return NULL;
	}
  }
  
  return self;
}

static int fileDelete(file_t *self) {
  int res = -1;
  if (self) {
	free(self->filename);
	self->filename = NULL;
	free(self);
	res = 0;
  }
  return res;
}

static int fileClose(file_t *self) {
  int res = 0;
  if (self && self->fd) {
	res = close(self->fd);
	if (!res)
	  self->fd = NULL;
  }
  return res;
}

static int getTempFilename(char **filename) {
  int err = 0;
  
  if (!filename) {
	err = EINVAL;
	goto exit0;	  	
  }
  if (*filename) {
	free(*filename);
  }
  *filename = malloc(FILE_TEMPLATE_LENGTH);
  if (!*filename) {
	err = errno;
	goto exit0;	  
  }
  strcpy(*filename, FILE_TEMPLATE);
  if (mkstemp(*filename) == -1) {
	err = errno;
	goto exit0;	  
  }

 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
	if (filename && *filename) {
	  free(*filename);
	  *filename = NULL;
	}
  }
  return err;
}

// wavfile
static int wavfileCheck(wavfile_t *self) {
  struct stat statbuf;	
  int err = 0;
  file_t *f = NULL;

  if (!self || !self->file) {
	err = EINVAL;
	goto exit0;	  
  }
  f = self->file;

  if (f->filename) {
	FILE *fdo = fopen(f->filename, "w");
	if (!fdo)  {
	  err = errno;
	  goto exit0;	  
	}
	fclose(fdo);
	return 0;
  }

  if (fstat(STDOUT_FILENO, &statbuf)) {
	err = errno;
	goto exit0;	  
  }
  
  if (S_ISREG(statbuf.st_mode)) {
	f->filename = realpath("/proc/self/fd/1", NULL);
	if (!f->filename) {
	  err = errno;
	  goto exit0;	  
	}
  } else if (S_ISFIFO(statbuf.st_mode)) {
	err = getTempFilename(&f->filename);
	if (err) {
	  goto exit0;	  
	}
	f->isFifo = true;
  } else {
	err = EINVAL;
  }

 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
  return err;
}

int wavfileDelete(void *handle) {
  ENTER();
  
  wavfile_t *self = handle;
  
  if(!self)
	return 0;

  if (self->file) {
	fileDelete(self->file);
	self->file = NULL;
  }
  free(self);
  return 0;
}

void *wavfileCreate(const char *filename, bool withHeader) {
  ENTER();

  wavfile_t *self = calloc(1, sizeof(*self));
  if (!self)
	goto exit0;

  self->withHeader = withHeader;
  self->file = fileCreate(filename, true);
  if (!self->file)
	goto exit0;
  
  if (wavfileCheck(self))
	goto exit0;
  
  return self;

 exit0:
  wavfileDelete(self);
  return NULL;
}

int wavfileSetHeader(void *handle, uint32_t wavSize, uint32_t rate) {
  uint32_t rawSize = 0;
  wavfile_t *self = handle;
  file_t *f = NULL;
  int err = 0;
  wav_header_t *w;
  
  if (!self || !self->file) {
	return EINVAL;
  }
  
  f = self->file;
  
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
  w->sampleRate = rate;
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


  if (f->fd) {
	fclose(f->fd);
  }
  
  f->fd = fopen(f->filename, "r+");
  if (!f->fd) {
	err = errno;
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);  
	goto exit0;	  
  }
  
  size_t i = fwrite(w, 1, sizeof(*w), f->fd);
  // TODO
  if (i != sizeof(w)) {
	err("%lu written (%lu expected)", i, (long unsigned int)sizeof(w));
	err = EIO;
	goto exit0;
  }

 exit0:
  return err;  
}

int wavfileGetDataSize(void *handle, size_t *wavSize) {
  wavfile_t *self = handle;
  file_t *f = NULL;
  struct stat statbuf;	

  if (!self || !wavSize || !self->file || !self->file->filename)
	return 1;

  f = self->file;  
	
  if (stat(f->filename, &statbuf))
	return 1;

  *wavSize = statbuf.st_size;
  
  return 0;
}

int wavfileClose(void *handle) {
  wavfile_t *self = handle;

  if (!self || !self->file)
	return EINVAL;

  return fileClose(file->fd);
}

// textfile

static int sentenceSearchLast(long *length) {
  int err = 0;
  
  if (!length) {
	return EINVAL;
  }
  
  if (*length > 2) {
	long length0 = *length;
	int i;
	int found=0;
	for (i=*length-2; i>0; i--) {
	  if ((tempbuf[i] == '.') && isspace(tempbuf[i+1])) {
		found = 1;
		length0 = i+2;
		break;
	  }
	}
	if (!found) {
	  for (i=*length-2; i>0; i--) {
		if (!isspace(tempbuf[i]) && isspace(tempbuf[i+1])) {
		  length0 = i+2;
		  break;
		}
	  }
	}
	*length = length0;
  }
  return err;
}

static int textfileCheck(textfile_t *self) {
  struct stat statbuf;	
  int err = 0;
  FILE *fd = NULL;
  file_t *f = NULL;

  if (!self || !self->file) {
	err = EINVAL;
	goto exit0;	  
  }

  f = self->file;
  
  if (!f->filename) {
	if (!*tempbuf) {
	  strcpy(tempbuf, "Hello World!");
	}
	err = getTempFilename(&f->filename);
	if (err) {
	  goto exit0;
	}
	fd = fopen(f->filename, "w");
	if (fd) {
	  fwrite(tempbuf, 1, strnlen(tempbuf, MAX_CHAR), fd);
	  fclose(fd);	  
	  f->isFifo = true;
	} else {
	  err = errno;
	  goto exit0;
	}
  }

  if (stat(f->filename, &statbuf) == -1) {
	err = errno;
  }
  
 exit0:
  if (fd) {
	fclose(fd);
  }
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
  return err;
}

static int textfileSentenceGet(textfile_t *self, region_t r, long *length) {
  ENTER();
  int err = 0;
  long max, x;  
  file_t *f = NULL;
  
  msg("[%d] ENTER %s", getpid(), __func__);
  
  if (!self || !self->file || !length || !self->file->fd) {
	err = EINVAL;
	goto exit0;
  }

  f = self->file;
  
  *length = 0;
  *tempbuf = 0;
  
  if (r.end <= r.begin) {
	goto exit0;
  }
  x = r.end - r.begin;
  max = (x < MAX_CHAR) ? x : MAX_CHAR;  

  if (fseek(f->fd, r.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }
  *length = fread(tempbuf, 1, max, f->fd);
  err = sentenceSearchLast(length);
  if (err) {
	goto exit0;
  }  
  tempbuf[*length] = 0;
  msg("[%d] read from=%ld, to=%ld", getpid(), r.begin, r.end);

 exit0:
  if (err) {
	err("%s",strerror(err));
  }
  return err;
}

int textfileSentenceGetPosPrevious(void *handle, region_t *r) {
  ENTER();
  textfile_t *self = handle;
  int max = 0;
  long range = 0;
  int err = 0;
  file_t *f = NULL;
  
  if (!self || !r || !self->file || !self->file->fd) {
	err = EINVAL;
	goto exit0;
  }

  f = self->file;
  range = r->end - r->begin;
  if (range < 0) {
	err = EINVAL;
	goto exit0;
  }
  max = (range < MAX_CHAR) ? range : MAX_CHAR;

  {
	long length = 0;
	region_t r0;
	r0.begin = r->begin + (r->end - max); 
	r0.end = r->end; 
	err = textfileSentenceGet(self, r0, &length);
	if (err) {
	  goto exit0;
	}
	r->end = r0.begin + length;
  }
  
 exit0:
  if (err) {
	err("%s",strerror(err));
  }
  return err;
}

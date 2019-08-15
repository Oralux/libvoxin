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

typedef struct {
  file_t *file;
  region_t *region;
} textfile_t;

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

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
	err = fileGetTemp(&f->filename);
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

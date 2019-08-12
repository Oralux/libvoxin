#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "file.h"
#include <sys/types.h>
#include <unistd.h>
#include "errno.h"
#include "debug.h"

typedef struct {
  char *filename;
  FILE *fd;
  int temporary;
} file_t;

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

static int sentenceSearchLast(long *length)
{
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

void *fileCreate(const char *filename, bool writable) {
  file_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;

  self->filename = strdup(filename);
  if (!self->filename) {
	free(self);
	return NULL;
  }

  return self;
}

int fileDelete(void *handle) {
  file_t *self = handle;
  int res = -1;
  if (self) {
	free(self->filename);
	self->filename = NULL;
	free(self);
	res = 0;
  }
  return res;
}


int fileSentenceGet(void *handle, region_t r, long *length) {
  ENTER();
  file_t *self = handle;
  int err = 0;
  long max, x;

  msg("[%d] ENTER %s", getpid(), __func__);
  
  if (!self || !length || !self->fd) {
	err = EINVAL;
	goto exit0;
  }

  *length = 0;
  *tempbuf = 0;
  
  if (r.end <= r.begin) {
	goto exit0;
  }
  x = r.end - r.begin;
  max = (x < MAX_CHAR) ? x : MAX_CHAR;  

  if (fseek(self->fd, r.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }
  *length = fread(tempbuf, 1, max, self->fd);
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

int fileSentenceGetPosPrevious(void *handle, region_t *r) {
  ENTER();
  file_t *self = handle;
  int max = 0;
  long range = 0;
  int err = 0;
  
  if (!self || !r || !self->fd) {
	err = EINVAL;
	goto exit0;
  }
  
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
	err = fileSentenceGet(self, r0, &length);
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

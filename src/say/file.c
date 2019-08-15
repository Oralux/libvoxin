#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "file.h"
#include "errno.h"
#include "debug.h"

#define FILE_TEMPLATE "/tmp/voxin-say.XXXXXXXXXX"
#define FILE_TEMPLATE_LENGTH 30

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

static int fileClose(file_t *self) {
  int res = 0;
  if (!self)
	return 0;
  if (self->fd) {
	res = fclose(self->fd);
	if (!res)
	  self->fd = NULL;
  }
  return res;
}

static int fileOpen(file_t *self, int mode) {
  int err = 0;
  if (!self) {
	return EINVAL;
  }

  if (self->fd) {
	if (self->fifo)
	  return 0;
	if ((self->mode & mode) & (FILE_READABLE|FILE_WRITABLE)) {
	  if (!(self->mode & FILE_APPEND))
		rewind(self->fd);
	  return 0;
	} else {
	  fclose(self->fd);
	  self->fd = 0;
	}	
  }
  
  if (self->fifo) {
	self->fd = (self->mode & FILE_READABLE) ? stdin : stdout;
	return 0;
  }

  char *m;
  if (mode & FILE_READABLE)
	m = "r";
  else if (mode & FILE_APPEND)
	m = "a";
  else
	m = "w";
  self->fd = fopen(self->filename, m);
  return (!self->fd) ? 0 : errno;
}

static int fileGetTemp(file_t *self) {
  int err = 0;
  int fd = -1;

  if (!self)
	return EINVAL;
  
  if (self->fd) {
	fclose(self->fd);
	self->fd = NULL;	
  }

  if (self->filename) {
	free(self->filename);
	self->filename = NULL;
  }
  self->len = 0;
  self->fifo = false;
  self->mode = FILE_READABLE;
  self->unlink = true;
	  
  self->filename = malloc(FILE_TEMPLATE_LENGTH);
  if (!self->filename) {
	err = errno;
	goto exit0;
  }
  strcpy(self->filename, FILE_TEMPLATE);
  fd = mkstemp(self->filename);
  if (fd == -1) {
	err = errno;
	goto exit0;
  }

  self->fd = fdopen(fd, "rw");
  if (!self->fd) {
	err = errno;
	goto exit0;
  }

  return 0;
  
 exit0:
  if (self->filename) {
	free(self->filename);
	self->filename = NULL;
  }

  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }

  return err;
}

file_t *fileCreate(const char *filename, int mode, bool fifo) {
  file_t *self = calloc(1, sizeof(*self));

  if (!self)
	return NULL;

  if (filename) {
	self->filename = strdup(filename);
	if (!self->filename) {
	  goto exit0;
	}
  } else if (fifo) {
	self->fifo = true;
  } else if (fileGetTemp(self)) {
	  goto exit0;
  }

  self->mode = mode;  
  return self;
 exit0:
	  free(self);
	  return NULL;
}

int fileDelete(file_t *self) {
  int res = -1;
  if (!self)
	return 0;
  if (self->filename) {
	if (self->unlink)
	  unlink(self->filename);
	free(self->filename);
	self->filename = NULL;
	free(self);
	res = 0;
  }
  return res;
}

int fileWrite(file_t *self, uint8_t *data, size_t len) {
  int res = EINVAL;
  if (self && self->fd && (self->mode & FILE_WRITABLE)) {
	res = EIO;
	int nb_items = fwrite(data, 1, len, self->fd);
	if (nb_items == 1) {
	  self->len += len;
	}
  }
  return res;
}

int fileRead(file_t *self, uint8_t *data, size_t len) {
  int res = EINVAL;
  if (self && self->fd && (self->mode & FILE_READABLE)) {
	res = EIO;
	int nb_items = fread(data, 1, len, self->fd);
	if (nb_items == 1) {
	  self->len += len;
	}
  }
  return res;
}



int fileAppend(file_t *self, file_t *src) {
  int err=0;
  FILE *fdout = NULL;
  size_t size;

  if (!self || !src) {
	return EINVAL;
  }

  err = fileOpen(src, FILE_READABLE);
  if (err)
	goto exit0;
  
  err = fileOpen(self, FILE_WRITABLE|FILE_APPEND);
  if (err)
	goto exit0;
  
  while(size) {
	// TODO checks
	fileRead(self, tempbuf, MAX_CHAR);
	fileWrite(self, tempbuf, size);
  }
  fileClose(src);
  
 exit0:
  if (err) {
	err("%s", strerror(err));
  }
  return err;
}

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "file.h"
#include "errno.h"
#include "debug.h"

#define FILE_TEMPLATE "/tmp/voxin-say.XXXXXX"
#define FILE_TEMPLATE_LENGTH 30

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

int fileClose(file_t *self) {
  ENTER();
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
  ENTER();
  int err = 0;
  if (!self) {
	return EINVAL;
  }

  if (self->fifo) {
	self->fd = (self->mode & FILE_READABLE) ? stdin : stdout;
	return 0;
  }

  if (self->fd)
	return EIO;
  
  char *m;
  if (mode & FILE_READABLE)
	m = "r";
  else if (mode & FILE_APPEND)
	m = "a";
  else
	m = "w";
  self->mode = mode;
  self->read = self->written = 0;
  self->fd = fopen(self->filename, m);
  return self->fd ? 0 : errno;
}

static int fileGetTemp(file_t *self, int mode) {
  ENTER();
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
  self->read = self->written = 0;
  self->fifo = false;
  self->mode = mode;
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

  self->fd = fdopen(fd, "r+");
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
  }

  return err;
}

file_t *fileCreate(const char *filename, int mode, bool fifo) {
  ENTER();
  file_t *self = calloc(1, sizeof(*self));

  if (!self)
	return NULL;

  self->mode = mode;  

  if (filename) {
	self->filename = strdup(filename);
	if (!self->filename)
	  goto exit0;
  } else if (fifo) {
	self->fifo = true;
  } else if (fileGetTemp(self, mode))
	goto exit0;

  if (!self->fd) {
	if (fileOpen(self, mode))
	  goto exit0;
  }

  return self;

 exit0:
  fileDelete(self);
  return NULL;
}

int fileDelete(file_t *self) {
  ENTER();
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

int fileWrite(file_t *self, const uint8_t *data, size_t len) {
  int res = 0;
  size_t x = 0;

  if (!self || !self->fd || !(self->mode & FILE_WRITABLE))
	return EINVAL;

  while(x < len) {
	size_t y = fwrite(data+x, 1, len-x, self->fd);
	x += y;
	if ((y < len-x) && ferror(self->fd)) {
	  res = EIO;
	  break;
	}
  }
  self->written += x;
  return res;
}

int fileRead(file_t *self, uint8_t *data, size_t len) {
  int res = 0;
  size_t x = 0;

  if (!self || !self->fd || !(self->mode & FILE_READABLE))
	return EINVAL;

  while(x < len) {
	size_t y = fread(data+x, 1, len-x, self->fd);
	x += y;
	if (y < len-x) {
	  if (feof(self->fd))
		break;
	  if (ferror(self->fd)) {
		res = EIO;
		break;
	  }
	}
  }
  self->read += x;

  return res;
}

int fileFlush(file_t *self) {
  int res = EINVAL;
  if (self && self->fd && (self->mode & FILE_WRITABLE))
	res = fflush(self->fd) ? errno : 0;
  return res;
}

int fileCat(file_t *self, file_t *src) {
  ENTER();
  int err=0;
  FILE *fdout = NULL;
  size_t size = 0;

  if (!self || !src) {
	return EINVAL;
  }

  if (src->fd)
	fileClose(src);
  
  err = fileOpen(src, FILE_READABLE);
  if (err)
	goto exit0;

  {
	struct stat statbuf;
	if (fstat(fileno(src->fd), &statbuf) == -1) {
	  err = errno;
	  goto exit0;
	}
	size = statbuf.st_size;
  }

  if (self->fd) {
	if ((self->mode & (FILE_WRITABLE|FILE_APPEND)) != (FILE_WRITABLE|FILE_APPEND)) {
	  fclose(self->fd);
	  self->fd = NULL;
	}
  }

  if (!self->fd) {
	err = fileOpen(self, FILE_WRITABLE|FILE_APPEND);
	if (err)
	  goto exit0;
  }
  
  while(size) {
	size_t oldread = src->read;
	size_t len = (size > MAX_CHAR) ? MAX_CHAR : size;
	size -= len;
	err = fileRead(src, tempbuf, len);
	if (err)
	  break;
	err = fileWrite(self, tempbuf, src->read - oldread);
	if (err)
	  break;
  }
  fileClose(src);

 exit0:
  if (err) {
	err("%s", strerror(err));
  }
  return err;
}

int fileGetSize(file_t *self) {
  ENTER();

  int size = -1;
  struct stat statbuf;

  if (!self || !self->filename)
	return -1;

  if (stat(self->filename, &statbuf) == -1) {
	int err = errno;
	err("%s", strerror(err));
  } else {
	size = statbuf.st_size;
  }
  
 exit0:
  return size;  
}

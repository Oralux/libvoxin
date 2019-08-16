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
// below this size, do not create other parts
#define MIN_PART_SIZE 256

typedef struct {
  long begin;
  long end;
} region_t;

typedef struct {
  file_t *file;
  region_t region;
} input_t;

typedef struct {
  input_t **input;
  size_t len; // usable number of elements in the input array
  size_t max; // max number of elements allocated
} textfile_t;

#define MAX_CHAR 10240
static char tempbuf[MAX_CHAR+10];

static input_t *getInput(textfile_t *self, size_t part) {
  return (!self || !self->input || (part >= self->len))
	? NULL : self->input[part];
}

static file_t *getFile(textfile_t *self, size_t part) {
  input_t *input = getInput(self, part);
  return  input ? input->file : NULL;
}

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


static int sentenceGet(textfile_t *self, size_t part, long *length) {
  ENTER();
  int err = 0;
  long max, x;
  file_t *f = getFile(self, 0);
  input_t *input = getInput(self, 0);
  region_t *r = NULL;
  msg("[%d] ENTER %s", getpid(), __func__);
  
  if (!f|| !length ) {
	err = EINVAL;
	goto exit0;
  }

  r = &input->region;
  *length = 0;
  *tempbuf = 0;
  
  if (r->end <= r->begin) {
	goto exit0;
  }
  x = r->end - r->begin;
  max = (x < MAX_CHAR) ? x : MAX_CHAR;  

  if (fseek(f->fd, r->begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }
  
  while(!*length) {
	*length = fread(tempbuf, 1, max, f->fd);
	
	if (!*length) {
	  if (feof(f->fd))
		goto exit0;
	  else if (ferror(f->fd)) {
		err = EIO;
		goto exit0;
	  }
	}
  }
  
  err = sentenceSearchLast(length);
  if (err)
	goto exit0;

  tempbuf[*length] = 0;
  msg("[%d] read from=%ld, to=%ld", getpid(), r->begin, r->end);

 exit0:
  if (err)
	err("%s",strerror(err));
  return err;
}

static int sentenceGetPosPrevious(textfile_t *self, region_t *r) {
  ENTER();
  int max = 0;
  long range = 0;
  int err = 0;
  file_t *f = getFile(self, 0);
  
  if (!f || !r) {
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
	err = sentenceGet(self, 0, &length);
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

static int sentenceSet(textfile_t *self, const char *sentence) {
  ENTER();

  int err = 0;
  input_t *input = getInput(self, 0);
  
  if (!input || !sentence || !*sentence)
	return EINVAL;
 
  input->file = fileCreate(NULL, FILE_READABLE|FILE_WRITABLE, false);
  file_t *f = input->file;
  if (f) {
	err = fileWrite(f, sentence, strlen(sentence));
	if (err) {
	  fileDelete(f);
	  input = NULL;
	}
  } else {
	err = EIO;
  }
  return err;
}

void *textfileCreate(const char *inputfile, unsigned int *number_of_parts, const char *sentence) {
  ENTER();

  textfile_t *self = calloc(1, sizeof(*self));
  file_t *f = NULL;
  input_t *input = NULL;
  int i;
  long partlen = 0;

  if (!self)
	goto exit0;

  if (!number_of_parts || (*number_of_parts < 1))
	goto exit0;

  self->max = self->len = *number_of_parts;
  self->input = calloc(*number_of_parts, *number_of_parts*(sizeof(*self->input)));
  if(!self->input)
	goto exit0;
  
  input = getInput(self, 0); 
  if (input) { // check input
	struct stat statbuf;
	if (inputfile) {
	  input->file = fileCreate(inputfile, FILE_READABLE, false);
	} else if (sentence) {
	  sentenceSet(self, sentence);
	} else if (fstat(STDIN_FILENO, &statbuf)) {
	  // no action
	} else if (S_ISREG(statbuf.st_mode)) {
	  inputfile = realpath("/proc/self/fd/0", NULL);
	  if (inputfile) {
		input->file = fileCreate(inputfile, FILE_READABLE, false);
	  }
	} else if (S_ISFIFO(statbuf.st_mode)) {
	  input->file = fileCreate(NULL, FILE_READABLE, true);
	} else {
	  sentenceSet(self, "Hello World!");
	}
  }

  if (!input || !input->file)
	goto exit0;

  f = input->file;
  
  if (!f->fifo) {
	struct stat statbuf;
	if (fstat(fileno(f->fd), &statbuf) == -1) {
	  goto exit0;	  
	} else {
	  input->region.begin = 0;
	  input->region.end = statbuf.st_size;
	}
  }

  if (input->region.end < MIN_PART_SIZE) {
	self->len = *number_of_parts = 1;
	return self;
  }	
  
  for (i=1; i<self->len; i++) {
	input_t *input = getInput(self, i);
	input->file = fileCreate(NULL, FILE_READABLE|FILE_WRITABLE, false);
	if (!input->file)
	  goto exit0;
  }

  region_t r = {0};
  region_t r0;
  /* r.begin = r.end = 0; */
  r0.begin = input->region.begin;
  r0.end = input->region.end;

  partlen = input->region.end/self->len;

  for (i=0; i<self->len; i++) {
	input_t *input = getInput(self, i);
	r.begin = r.end;
	if (i == self->len-1) {
	  r.end = r0.end;	  
	} else {
	  r.end = r.begin + partlen;
	  if (sentenceGetPosPrevious(self, &r) == -1) {
		goto exit0;
	  }
	}
	if (!i) {
	  r.begin = 0;
	  //	  r.end = r.end;
	}

	input->region.begin = r.begin;
	input->region.end = r.end;
	if (fseek(input->file->fd, r.begin, SEEK_SET) == -1) {
	  goto exit0;
	}	
  }
   
  return self;

 exit0:
  textfileDelete(self);
  return NULL;
}

int textfileDelete(void *handle) {  
  ENTER();
  int err = 0;
  int i;
  textfile_t *self = (textfile_t *)handle;

  if (!self)
	return 0;
  
  for (i=0; i<self->len; i++) {
	input_t *input = self->input[i];
	err = fileDelete(input->file);
	if (err)
	  goto exit0;
	input->file = NULL;
	free(input);
	self->input[i] = NULL;
  }
  free(self->input);
  self->input = NULL;
  free(self);
  return 0;

 exit0:
  return -1;
}

int textfileSentenceGet(void *handle, unsigned int part, long *length) {
  ENTER();
  textfile_t *self = (textfile_t*)handle;
  input_t *input = getInput(self, 0);

  if (!input || !length)
	return EINVAL;
  
  region_t *r = &input->region;
  int err = sentenceGet(self, part, length);

  if (!err && length)
	r->begin += *length;

  return err;
}


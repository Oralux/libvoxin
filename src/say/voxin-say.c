/* This file is under the LGPL license */

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "textfile.h"
#include "wavfile.h"
#include "tts.h"
#include "debug.h"

#define MAX_JOBS 32
static char tempbuf[MAX_CHAR+10];
#define MAX_LANG 100

void usage()
{
  fprintf(stderr, "Usage: voxin-say [OPTION]... [text]\n \
 \n\
voxin-say (version %s) \n\
Converts text to speech written to the standard output or the\n\
supplied file.\n\
\n\
EXAMPLES :\n\
\n\
# Say 'hello world' and redirect output to an external audio player:\n\
./voxin-say \"hello world\" | aplay\n\
# Read file.txt and save speech to an audio file:\n\
./voxin-say -f file.txt -w file.wav\n\
./voxin-say -f file.txt > file.wav\n\
# The following command is incorrect because no output is supplied:\n\
./voxin-say \"Hello all\"\n\
# Correct command to read a file in French at 500 words per minute,\n\
  use 4 jobs to speed\n\
  up conversion\n\
./voxin-say -f file.txt -l fr -s 500 -j 4 -w audio.wav\n\
\n\
\n\
OPTIONS :\n\
  -f FILE   supply the UTF-8 text file to read. \n\
  -j NUM    number of jobs, help to share the workload on several \n\
            processes to speedup conversion. \n\
  -l NAME   select voice/language. \n\
  -L        list installed voices/languages. \n\
  -s NUM    speed in words per minute (from 0 to 1297). \n\
  -S NUM    speed in units (from 0 to 250). \n\
  -w FILE   supply the output wavfile. \n\
  -d        for debug, wait in an infinite loop. \n\
", VERSION);
}


typedef struct {
  void *text;
  region_t region;
  void *tts;
  void *wav[MAX_JOBS];
  int jobs;
} obj_t;

#define getSpeedUnits(i) ((i<0) ? 0 : ((i>250) ? 250 : i))

/* voices returned by libvoxin, some values (name, quality) can be
   slightly modified:
   - quality is appended to name to differentiate distinct voices with
   same name but distinct qualities
   - if quality is unset (empty), it is set to "none"
*/

static int copyFile(const char *filename, FILE *dest)
{
  int size = 1;
  int err=0;
  FILE *src = NULL;

  if (!filename || !dest) {
	err = EINVAL;
	goto exit0;
  }

  src = fopen(filename, "r");
  if (!src) {
	err = errno;
	goto exit0;
  }
  
  while(size) {
	// TODO checks
	size = fread(tempbuf, 1, MAX_CHAR, src);
	fwrite(tempbuf, 1, size, dest);
  }
  fclose(src);
  
 exit0:
  if (err) {
	err("%s", strerror(err));
  }
  return err;
}

// text: 16 char min
static int synthSay(void *tts, char *text)
{
  int err = EINVAL;
  ENTER();
  msg("[%d] ENTER %s", getpid(), __func__);

  if (!tts || !text) {
	return EINVAL;
  }

  ttsSay(tts, text);

 exit0:  
  return err;
}

static void sentenceCreate(const char *s)
{
  *tempbuf = 0;
  if (s) {
  	strncpy(tempbuf, s, MAX_CHAR);	  
	tempbuf[MAX_CHAR] = 0;
  }  
}

static int objUpdateHeaderWav(obj_t *self) {
  long wavSize = 0;
  int i;
  int err = 0;

  ENTER();
  
  for (i=0; i<self->jobs; i++) {
	size_t s = 0;
	err = wavfileGetDataSize(self->wav+i, &s);
	if (err)
	  goto exit0;	  
	wavSize += s;
  }

  err = wavfileSetHeader(&self->wav, wavSize, ttsGetRate(self->tts));
  
 exit0:
  return err;
}

static int objFlushWav(obj_t *self) {
  int i;
  int err = 0;
  FILE *fdo = NULL;

  ENTER();

  for (i=0; i<self->jobs; i++) {
	wavfileClose(self->wav[i]);
  }

  set output a la création : soit stdout si fifo ou wav0 sinon (hormis le 1er elt 0)
  
  if (wavfileIsFifo(self->wav[0])) {
	fdo = stdout;
	wavfileCopy(self->wav[0], fdo);
  } else if (self->jobs > 1) {
	fdo = fopen(self->wav[0].filename, "a");
  } else {
	goto exit0;
  }

  for (i=1; i<self->jobs; i++) {
	wavfileCopy(self->wav[i], fdo);
  }
  
 exit0:
  if ((fdo != stdout) && fdo) {
	fclose(fdo);
  }
  if (err) {
	err("%s", strerror(err));
  }  
  return err;
}

static int objSayText(obj_t *self, region_t r, char* output, int withWavHeader) {
  long length = 0;
  int err = 0;  
  
  if (!output) {
	err = EINVAL;
	goto exit0;
  }
  
  if (self->wav[0].fd) {
	fclose(self->wav[0].fd);
	self->wav[0].fd = NULL;
  }
  
  if (self->text.fd) {
	fclose(self->text.fd);
	self->text.fd = NULL;
  }

  self->region.begin = r.begin;
  self->region.end = r.end;
  self->wav[0].filename = output;
  self->withWavHeader = withWavHeader;
  
  msg("[%d] begin=%ld, end=%ld [%s] ", getpid(), r.begin, r.end, output);

  self->text.fd = fopen(self->text.filename, "r");    
  if (!self->text.fd) {
	err = errno;
	goto exit0;
  }

  if (fseek(self->text.fd, self->region.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }

  self->wav[0].fd = fopen(self->wav[0].filename, "w");
  if (!self->wav[0].fd) {
	err = errno;
	goto exit0;
  }
  if (self->withWavHeader) {
	wav_header_t w;
	memset(&w, 0, sizeof(w));
	fwrite(&w, 1, sizeof(w), self->wav[0].fd);
  }
  
  if (!self->tts) {
	if (ttsInit(self->tts, self->wav[0].fd)) {
	  err = EIO;
	  goto exit0;
	}
  }
  
  length = 1;
  while(length) {
	err = sentenceGet(self->text, self->region, &length);
	if (err) {	  
	  break;
	}	
  	if (!length) {
  	  if (feof(self->text.fd)) {
  		err=0;
  		break;
  	  } else if (ferror(self->text.fd)) {
  		err("file error: %s", self->text.filename);
		err = EIO;
  		break;
  	  }
  	} else {
	  synthSay(&self->tts, tempbuf);
	}
	self->region.begin += length;
  }

 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
  if (self->wav[0].fd) {
	fclose(self->wav[0].fd);
	self->wav[0].fd = NULL;
  }  
  if (self->text.fd) {
	fclose(self->text.fd);
	self->text.fd = NULL;
  }
  return err;
}

static int objSay(obj_t *self) {
  long partlen = 0;
  pid_t pid[MAX_JOBS];
  int i = 0;
  int err = 0;
  region_t r, r0;
  r.begin = r.end = 0;
  r0.begin = r0.end = 0;
    
  if (!self->text.filename || !self->jobs || (self->jobs > MAX_JOBS) || !self->wav[0].filename) {
	err = EINVAL;
	goto exit0;
  }
	
  if (self->region.end < self->jobs) {
	self->jobs = 1;
  }
	  
  partlen = self->region.end/self->jobs;

  for (i=0; i<self->jobs; i++) {
	const char* fmt = "%s.part%d.raw";
	r.begin = r.end;
	if (i == self->jobs-1) {
	  r.end = self->region.end;
	} else {
	  r.end = r.begin + partlen;
	  if (sentenceGetPosPrevious(self->text, &r, obj->text.fd) == -1) {
		err = EINVAL;
		goto exit0;
	  }
	}
	if (!i) {
	  r0.begin = 0;
	  r0.end = r.end;
	  continue;
	}
	if (self->wav[i].filename) {
	  free(self->wav[i].filename);
	}
	self->wav[i].filename = (char*)malloc(strlen(self->wav[0].filename) + strlen(fmt) + 10);
	if (!self->wav[i].filename) {
	  err = errno;
	  goto exit0;
	}
	sprintf((char *)self->wav[i].filename, fmt, self->wav[0].filename, i);
	self->wav[i].temporary = 1;
	pid[i] = fork();
	if (!pid[i]) {	  
	  err = selfSayText(r, self->wav[i].filename, 0);
	  exit(err);
	}	
	msg("[%d] child pid=%d, begin=%ld, end=%ld [%s] ", getpid(), pid[i], self->region.begin, self->region.end, (self->wav[i].filename) ? self->wav[i].filename : "null");	  
  }

  err = objSayText(r0, self->wav[0].filename, self->withWavHeader);

  for (i=1; i < self->jobs; i++) {
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
	
  err = objUpdateHeaderWav();
  if (err) {
	goto exit0;
  }
  err = objFlushWav();
	
 exit0:
  return err;
}

static obj_t *objCreate(char *input, char *output, int jobs, char *voiceName, int speed) {
  ENTER();

  obj_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;

  self->ttsCreate(voiceName, speed);
  
  self->text = textfileCreate(input);
  if (!self->text
	goto exit0;
  self->wav[0] = wavfileCreate(output, true);
  if (!self->wav[0])
	goto exit0;
	
  self->jobs = jobs;
  self->tts = ttsCreate(voiceName, speed);

  if (self->text.filename) {
	struct stat statbuf;
	if (self->text.fd) {
	  fclose(self->text.fd);
	}
	self->text.fd = fopen(self->text.filename, "r");
	if (!self->text.fd) {
	  err = errno;
	  goto exit0;
	}
	if (stat(self->text.filename, &statbuf) == -1) {
	  err = errno;
	  goto exit0;	  
	}
	self->region.begin = 0;
	self->region.end = statbuf.st_size;
  }

 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }
  return err;
}

static void objDelete()
{
  int i;

  ENTER();
  
  if (self->text.fd) {
	fclose(self->text.fd);
	self->text.fd = NULL;
  }
  if (self->text.filename) {
	if (self->text.temporary) {
	  unlink(self->text.filename);
	}
	free(self->text.filename);
	self->text.filename = NULL;
  }
  
  for (i=0; i<MAX_JOBS; i++) {
	if (self->wav[i].fd) {
	  fclose(self->wav[i].fd);
	  self->wav[i].fd = NULL;
	}
	if (self->wav[i].filename) {
	  if (self->wav[i].temporary) {
		unlink(self->wav[i].filename);
	  }
	  free(self->wav[i].filename);
	  self->wav[i].filename = NULL;
	}
  }
  
  if (self->tts->handle) {
	eciDelete(self->tts->handle);
  }
}

int main(int argc, char *argv[])
{
  int debug = 0;
  int help = 0;
  char *input = NULL;
  char *output = NULL;
  int jobs = 1;
  int speed = SPEED_UNDEFINED;
  int opt;
  int temporaryOutput = 0;
  int fifo = 0;
  int err = EINVAL;
  int list = 0;
  char *voiceName = NULL;
 
  ENTER();
 
  while ((opt = getopt(argc, argv, "df:hj:l:Ls:S:w:")) != -1) {
    switch (opt) {
    case 'w':
	  if (output) {
		free(output);
	  }
      output = strdup(optarg);
      break;
      
    case 'f':
	  if (input) {
		free(input);
	  }
	  input = strdup(optarg);	  
      break;

    case 'j':
	  jobs = atoi(optarg);	  
      break;

    case 'h':
	  help = 1;	  
      break;

    case 'l':
	  if (voiceName) {
		free(voiceName);
	  }
	  voiceName = strdup(optarg);
      break;

    case 'L':
	  list = 1;	  
      break;

    case 'S':
	  speed = getSpeedUnits(atoi(optarg));
      break;

    case 's':
	  {
		int i = atoi(optarg);
		i = (i*2-140)/10;
		speed = getSpeedUnits(i);
	  }
      break;

    case 'd':
	  debug = 1;	  
      break;

    default:
      err = EINVAL;
	  goto exit0;
    }
  }

  if (debug) {
	while (debug) {
	  // to quit the loop, change debug var using gdb:
	  // set var debug=0
	  fprintf(stderr, "infinite loop for debug...\n");
	  sleep(5);
	}
  }

  if (help) {
	usage();
	goto exit0;
  }

  if (list) {
	voicePrintList(voice);
	err = 0;
	goto exit0;
  }
  
  {
	const char *s =  (optind == argc-1) ? argv[optind] : NULL;  
	sentenceCreate(s);
  }
  
  if ((jobs <= 0) || (jobs > MAX_JOBS)) {
	err = EINVAL;
	err("jobs=%d (limit=1..%d)", self->jobs, MAX_JOBS);
	goto exit0;
  }

  obj_t *self = objCreate(input, output, jobs, voice, voiceName, speed);
  if (!self) {
	usage();
	goto exit0;
  }

  objSay(self);
  
 exit0:
  objDelete(self);

  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }
  
  return 0;
}

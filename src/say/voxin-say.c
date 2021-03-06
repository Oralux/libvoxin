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
voxin-say \"hello world\" | aplay\n\
# Read file.txt and save speech to an audio file:\n\
voxin-say -f file.txt -w file.wav\n\
voxin-say -f file.txt > file.wav\n\
# The following command is incorrect because no output is supplied:\n\
voxin-say \"Hello all\"\n\
# Correct command to read a file in French at 500 words per minute,\n\
  use 4 jobs to speed\n\
  up conversion\n\
voxin-say -f file.txt -l fr -s 500 -j 4 -w audio.wav\n\
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
  void *tts;
  void *wav;
  int jobs;
  const char *voiceName;
  int speed;
} obj_t;

#define getSpeedUnits(i) ((i<0) ? 0 : ((i>250) ? 250 : i))

/* voices returned by libvoxin, some values (name, quality) can be
   slightly modified:
   - quality is appended to name to differentiate distinct voices with
   same name but distinct qualities
   - if quality is unset (empty), it is set to "none"
*/


static int objSayText(obj_t *self, int job) {
  ENTER();
  
  long length = 0;
  int err = 0;  
  
  if (!self) {
	err = EINVAL;
	goto exit0;
  }

  // tts must be created in each process
  if (!self->tts)
	self->tts = ttsCreate(self->voiceName, self->speed);
  if (!self->tts)
	goto exit0;
  
  err = ttsSetOutput(self->tts, self->wav, job);
  if (err)
	goto exit0;
  
  length = 0;
  do {
	const char *sentence = NULL;
	err = textfileGetNextSentences(self->text, job, &length, &sentence);
	if (err)
	  break;
  	if (length)
	  ttsSay(self->tts, sentence);
  } while(length);

 exit0:
  return err;
}

static int objSay(obj_t *self) {
  long partlen = 0;
  pid_t pid[MAX_JOBS];
  int i = 0;
  int err = 0;
    
  if (!self->text || !self->jobs || (self->jobs > MAX_JOBS) || !self->wav) {
	err = EINVAL;
	goto exit0;
  }
	
  for (i=1; i<self->jobs; i++) {
	pid[i] = fork();
	if (!pid[i]) {
	  err = objSayText(self, i);
	  exit(err);
	}	
	msg("child pid=%d, job=%d", pid[i], i);	  
  }

  err = objSayText(self, 0);
  if (err)
	goto exit0;
  
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

  wavfileSetRate(self->wav, ttsGetRate(self->tts));
  err = wavfileFlush(self->wav);
	
 exit0:
  return err;
}

static void objDelete(obj_t *self) {
  ENTER();
  if (!self)
	return;
  textfileDelete(self->text);
  ttsDelete(self->tts);
  wavfileDelete(self->wav);
  if (self->voiceName)
	free((char*)self->voiceName);
  free(self);
}

static obj_t *objCreate(const char *input, const char *output, int jobs, const char *voiceName, int speed, const char *sentence) {
  ENTER();

  obj_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;

  self->voiceName = voiceName ? strdup(voiceName) : NULL;
  self->speed = speed;
  self->text = textfileCreate(input, &jobs, sentence);
  if (!self->text)
	goto exit0;

  self->wav = wavfileCreate(output, jobs);
  // self->wav can be NULL (e.g. if only the list of voices is required)

  self->jobs = jobs;
  return self;

 exit0:
  objDelete(self);
  return NULL;
}

int main(int argc, char *argv[])
{
  int debug = 0;
  int help = 0;
  char *inputfile = NULL;
  char *outputfile = NULL;
  int jobs = 1;
  int speed = SPEED_UNDEFINED;
  int opt;
  int temporaryOutput = 0;
  int fifo = 0;
  int err = 0;
  int list = 0;
  char *voiceName = NULL;
  char *sentence = NULL;  
  obj_t *self = NULL;
	
  ENTER();

  while ((opt = getopt(argc, argv, "df:hj:l:Ls:S:w:")) != -1) {
    switch (opt) {
    case 'w':
	  if (outputfile) {
		free(outputfile);
	  }
      outputfile = strdup(optarg);
      break;
      
    case 'f':
	  if (inputfile) {
		free(inputfile);
	  }
	  inputfile = strdup(optarg);	  
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

  sentence = (optind == argc-1) ? strdup(argv[optind]) : NULL;  
  
  if ((jobs <= 0) || (jobs > MAX_JOBS)) {
	err = EINVAL;
	err("jobs=%d (limit=1..%d)", jobs, MAX_JOBS);
	goto exit0;
  }

  self = objCreate(inputfile, outputfile, jobs, voiceName, speed, sentence);
  if (!self) {
	usage();
	goto exit0;
  }

  if (list) {
	if (!self->tts)
	  self->tts = ttsCreate(voiceName, speed);
	if (!self->tts)
	  goto exit0;
	ttsPrintList(self->tts);
	err = 0;
	goto exit0;
  }
  
  objSay(self);
  
 exit0:
  objDelete(self);

  if (inputfile)
	free(inputfile);

  if (outputfile)
	free(outputfile);

  if (voiceName)
	free(voiceName);

  if (sentence)
	free(sentence);

  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }
  
  return 0;
}

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
#define SPEED_UNDEFINED -1
#define VOICE_UNDEFINED -1
static char tempbuf[MAX_CHAR+10];
#define FILE_TEMPLATE "/tmp/say.XXXXXXXXXX"
#define FILE_TEMPLATE_LENGTH 20
void usage()
{
  fprintf(stderr, "Usage: say OPTIONS \"optional text\"\n \
 \n\
say (version %s) \n\
Read the supplied text (utf-8) and writes speech to an audio file (WAV\n\
format) or an external audio player.\n\
\n\
EXAMPLES :\n\
\n\
./say \"hello world\" | aplay\n\
./say -f file.txt > audio.wav\n\
# Wrong command because no output is supplied\n\
./say \"Hello all\"\n\
\n\
# Read file.txt in French at 500 words per minute, use 4 jobs to speed\n\
  up conversion\n\
./say -f file.txt -l fr -s 500 -j 4 -w audio.wav\n\
#\n\
\n\
\n\
OPTIONS :\n\
  -w    the output wavfile (with header by default)	\n\
        say -w file.wav	\n\
        other ways to get the wav output:	\n\
        say > file.wav	\n\
        say | aplay	\n\
        say | paplay	\n\
  -f    text file to be spoken. \n\
  -j    number of jobs, share the workload on several \n\
        processes to speedup conversion. \n\
  -l    select voice/language\n\
  -L    list installed voices/languages\n\
  -s    speed in words per minute (from 0 to 1297) \n\
  -S    speed in units (from 0 to 250) \n\
  -d    for debug, wait in an infinite loop \n\
", VERSION);
}

typedef struct {
  int fd;
} data_cb_t;

static data_cb_t data_cb;


typedef struct {
  char *filename;
  FILE *fd;
  int temporary;
} file_t;

typedef struct {
  long begin;
  long end;
} region_t;

typedef struct {
  void *handle;
  short samples[MAX_SAMPLES];
  int speed; // SPEED_UNDEFINED if unset
  int voiceID; // VOICE_UNDEFINED if unset
} tts_t;

typedef struct {
  file_t text;
  region_t region;
  file_t wav[MAX_JOBS];
  int withWavHeader;
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

typedef struct {
  char *name;
  char *language;
  char *variant;
} voice_t;

#define getSpeedUnits(i) ((i<0) ? 0 : ((i>250) ? 250 : i))

// eciLocale, eciLocales from speech-dispatcher (ibmtts.c)
typedef struct _eciLocale {
	char *name;
	char *lang;
	char *dialect;
	enum ECILanguageDialect langID;
	char *charset;
} eciLocale, *eciLocaleList;

static eciLocale eciLocales[] = {
	{
	 "American_English", "en", "US", eciGeneralAmericanEnglish,
	 "ISO-8859-1"},
	{"British_English", "en", "GB", eciBritishEnglish, "ISO-8859-1"},
	{"Castilian_Spanish", "es", "ES", eciCastilianSpanish, "ISO-8859-1"},
	{"Mexican_Spanish", "es", "MX", eciMexicanSpanish, "ISO-8859-1"},
	{"French", "fr", "FR", eciStandardFrench, "ISO-8859-1"},
	{"Canadian_French", "ca", "FR", eciCanadianFrench, "ISO-8859-1"},
	{"German", "de", "DE", eciStandardGerman, "ISO-8859-1"},
	{"Italian", "it", "IT", eciStandardItalian, "ISO-8859-1"},
	{"Mandarin_Chinese", "zh", "CN", eciMandarinChinese, "GBK"},
	{"Mandarin_Chinese GB", "zh", "CN_GB", eciMandarinChineseGB, "GBK"},
	{
	 "Mandarin_Chinese PinYin", "zh", "CN_PinYin", eciMandarinChinesePinYin,
	 "GBK"},
	{"Mandarin_Chinese UCS", "zh", "CN_UCS", eciMandarinChineseUCS, "UCS2"},
	{"Taiwanese_Mandarin", "zh", "TW", eciTaiwaneseMandarin, "BIG5"},
	{
	 "Taiwanese_Mandarin Big 5", "zh", "TW_Big5", eciTaiwaneseMandarinBig5,
	 "BIG5"},
	{
	 "Taiwanese_Mandarin ZhuYin", "zh", "TW_ZhuYin",
	 eciTaiwaneseMandarinZhuYin, "BIG5"},
	{
	 "Taiwanese_Mandarin PinYin", "zh", "TW_PinYin",
	 eciTaiwaneseMandarinPinYin, "BIG5"},
	{
	 "Taiwanese_Mandarin UCS", "zh", "TW_UCS", eciTaiwaneseMandarinUCS,
	 "UCS2"},
	{
	 "Brazilian_Portuguese", "pt", "BR", eciBrazilianPortuguese,
	 "ISO-8859-1"},
	{"Japanese", "ja", "JP", eciStandardJapanese, "SJIS"},
	{"Japanese_SJIS", "ja", "JP_SJIS", eciStandardJapaneseSJIS, "SJIS"},
	{"Japanese_UCS", "ja", "JP_UCS", eciStandardJapaneseUCS, "UCS2"},
	{"Finnish", "fi", "FI", eciStandardFinnish, "ISO-8859-1"},
	{"Korean", "ko", "KR", eciStandardKorean, "UHC"},
	{"Korean_UHC", "ko", "KR_UHC", eciStandardKoreanUHC, "UHC"},
	{"Korean_UCS", "ko", "KR_UCS", eciStandardKoreanUCS, "UCS2"},
	{"Cantonese", "zh", "HK", eciStandardCantonese, "GBK"},
	{"Cantonese_GB", "zh", "HK_GB", eciStandardCantoneseGB, "GBK"},
	{"Cantonese_UCS", "zh", "HK_UCS", eciStandardCantoneseUCS, "UCS2"},
	{"HongKong_Cantonese", "zh", "HK", eciHongKongCantonese, "BIG5"},
	{
	 "HongKong_Cantonese Big 5", "zh", "HK_BIG5", eciHongKongCantoneseBig5,
	 "BIG5"},
	{
	 "HongKong_Cantonese UCS", "zh", "HK_UCS", eciHongKongCantoneseUCS,
	 "UCS-2"},
	{"Dutch", "nl", "BE", eciStandardDutch, "ISO-8859-1"},
	{"Norwegian", "no", "NO", eciStandardNorwegian, "ISO-8859-1"},
	{"Swedish", "sv", "SE", eciStandardSwedish, "ISO-8859-1"},
	{"Danish", "da", "DK", eciStandardDanish, "ISO-8859-1"},
	{"Reserved", "en", "US", eciStandardReserved, "ISO-8859-1"},
	{"Thai", "th", "TH", eciStandardThai, "TIS-620"},
	{"ThaiTIS", "th", "TH_TIS", eciStandardThaiTIS, "TIS-620"},
	{NULL, 0, NULL}
};

#define MAX_NB_OF_LANGUAGES (sizeof(eciLocales)/sizeof(eciLocales[0]) - 1)

static void WavSetHeader(wav_header_t *w, uint32_t wavSize)
{
  uint32_t rawSize = 0;

  if (!w) {
	return;
  }

  if (wavSize < sizeof(wav_header_t)) {
	wavSize = sizeof(wav_header_t);
  }

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

static int getTempFilename(char **filename)
{
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

static int checkInput(char **filename, int *temporary)
{
  struct stat statbuf;	
  int err = 0;

  if (!filename || !temporary) {
	err = EINVAL;
	goto exit0;	  
  }
  
  if (!*filename) {
	FILE *fd;
	if (!*tempbuf) {
	  strcpy(tempbuf, "Hello World!");
	}
	err = getTempFilename(filename);
	if (err) {
	  goto exit0;
	}
	fd = fopen(*filename, "w");
	if (fd) {
	  fwrite(tempbuf, 1, strnlen(tempbuf, MAX_CHAR), fd);
	  fclose(fd);	  
	  *temporary = 1;
	} else {
	  err = errno;
	  goto exit0;
	}
  }

  if (stat(*filename, &statbuf) == -1) {
	err = errno;
  }
  
 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
  return err;
}

static int checkOutput(char **filename, int *fifo)
{
  struct stat statbuf;	
  int err = 0;

  if (!filename || !fifo) {
	err = EINVAL;
	goto exit0;	  
  }

  *fifo = 0;
  if (*filename) {
	FILE *fdo = fopen(*filename, "w");
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
	*filename = realpath("/proc/self/fd/1", NULL);
	if (!*filename) {
	  err = errno;
	  goto exit0;	  
	}
  } else if (S_ISFIFO(statbuf.st_mode)) {
	err = getTempFilename(filename);
	if (err) {
	  goto exit0;	  
	}
	*fifo = 1;
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

static enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  data_cb_t *data_cb = (data_cb_t *)pData;

  if (data_cb && (Msg == eciWaveformBuffer)) {
	ssize_t len = write(data_cb->fd, obj.tts.samples, 2*lParam);
  }
  return eciDataProcessed;
}

static void *synthInit(tts_t *tts, FILE *fdo)
{
  ENTER();
  int err = 0;

  if (!tts || !fdo) {
	err = EINVAL;
	goto exit0;
  }

  tts->handle = eciNew();
  if (!tts->handle) {
	err("null handle");
	return NULL;
  }

  data_cb.fd = fileno(fdo);
  if (data_cb.fd == -1) {
	err = errno;
	goto exit0;
  }

  // enable dictionaries
  eciSetParam(tts->handle, eciDictionary, 0);

  /* enable ssml and punctuation filters */
  eciSetParam(tts->handle, eciInputType, 1);
  eciAddText(tts->handle, " `gfa1 ");
  eciAddText(tts->handle, " `gfa2 ");
  
  if (tts->voiceID != VOICE_UNDEFINED) {
	if (eciSetParam(tts->handle, eciLanguageDialect, tts->voiceID) == -1) {
	  err("error: set param %d to %d", eciLanguageDialect, tts->voiceID);
	  goto exit0;
	}
  }

  if (tts->speed != SPEED_UNDEFINED) {
	if (eciSetVoiceParam(tts->handle, 0, eciSpeed, tts->speed) == -1) {
	  err("error: set voice param %d to %d", eciSpeed, tts->speed);
	  goto exit0;
	}
  }
  
  eciRegisterCallback(tts->handle, my_client_callback, &data_cb);

  if (!eciSetOutputBuffer(tts->handle, MAX_SAMPLES, tts->samples)) {
	goto exit0;
  }

  return tts->handle;
  
 exit0:
  if (tts->handle) {
	eciDelete(tts->handle);
  }
  if (err) {
	err("%s", strerror(err));
  }
  return NULL;
}


static int synthSearchVoice(char *name, int *voiceID)
{
  int i;
  int err = EINVAL;

  if (!name || !voiceID) {
	err = EINVAL;
	goto exit0;
  }

  for (i=0; i<MAX_NB_OF_LANGUAGES; i++) {
	if (!strcasecmp(name, eciLocales[i].name) || !strcasecmp(name, eciLocales[i].lang)) {
	  *voiceID = eciLocales[i].langID;
	  err= 0;
	  break;
	}
  }

 exit0:
  if (err) {
	err("%s",strerror(err));
  }
  return err;	
}

static int synthConvertVoiceID(int voiceID, voice_t *v)
{
  int i;
  int err = EINVAL;

  if (!v) {
	err = EINVAL;
	goto exit0;
  }

  for (i=0; i<MAX_NB_OF_LANGUAGES; i++) {
	if (eciLocales[i].langID == voiceID) {
	  v->name = eciLocales[i].name;
	  v->language = eciLocales[i].lang;
	  v->variant = eciLocales[i].dialect;
	  err = 0;
	  break;
	}
  }

 exit0:
  if (err) {
	err("%s",strerror(err));
  }
  return err;	
}

static int synthGetVoices(voice_t *list, int *nbVoices)
{
  int err = 0;
  int i = 0;
  enum ECILanguageDialect *voiceID = NULL;
  int max = 0;

  if (!nbVoices || (*nbVoices && !list)) {
	err = EINVAL;
	goto exit0;
  }
  max = *nbVoices;
  if (max) {
	voiceID = calloc(max, sizeof(voiceID));
	if (!voiceID) {
	  err = errno;
	  goto exit0;
	}
  } else {
	static enum ECILanguageDialect foo;
	voiceID = &foo;
  }
  if (eciGetAvailableLanguages(voiceID, nbVoices)) {
	err = EINVAL;
	goto exit0;
  }

  for (i=0; i<max; i++) {
	synthConvertVoiceID(voiceID[i], list+i);
  }
  
 exit0:
  if (max && voiceID) {
	free(voiceID);
  }
  if (err) {
	err("%s",strerror(err));
  }
  return err;	
}

// text: 16 char min
static int synthSay(tts_t *tts, char *text)
{
  int err = EINVAL;
  ENTER();
  msg("[%d] ENTER %s", getpid(), __func__);

  if (!tts || !text) {
	return EINVAL;
  }

  if (!eciAddText(tts->handle, text)) {
	tempbuf[16] = 0;
	err("Error: add text=%s...", text);
  } else if (!eciSynthesize(tts->handle)) {
	err("Error: synth handle=0x%p", text);
  } else if (!eciSynchronize(tts->handle)) {
	err("Error: sync handle=0x%p", text);
  } else {
	err = 0;
  }

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

static int sentenceGet(region_t r, FILE *fd, long *length)
{
  int err = 0;
  long max, x;

  msg("[%d] ENTER %s", getpid(), __func__);
  
  if (!length || !obj.text.fd) {
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

  if (fseek(fd, r.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }
  *length = fread(tempbuf, 1, max, fd);
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

static int sentenceGetPosPrevious(region_t *r, FILE *fd)
{
  int max = 0;
  long range = 0;
  int err = 0;
  
  if (!r || !fd) {
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
	err = sentenceGet(r0, fd, &length);
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

static int objUpdateHeaderWav()
{
  wav_header_t w;
  long wavSize = 0;
  struct stat statbuf;
  int i;
  int err = 0;
  FILE *fd = NULL;

  ENTER();
  
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

  WavSetHeader(&w, wavSize);

  if (obj.wav[0].fd) {
	fclose(obj.wav[0].fd);
  }
  
  fd = fopen(obj.wav[0].filename, "r+");
  if (!fd) {
	err = errno;
	goto exit0;	  
  }
  
  i = fwrite(&w, 1, sizeof(w), fd);
  // TODO
  if (i != sizeof(w)) {
	err("%d written (%ld expected)", i, sizeof(w));
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

static int objFlushWav()
{
  int i;
  int err = 0;
  FILE *fdo = NULL;

  ENTER();

  for (i=0; i<obj.jobs; i++) {
	if (obj.wav[i].fd) {
	  fclose(obj.wav[i].fd);
	  obj.wav[i].fd = NULL;
	}
  }

  if (obj.wav[0].temporary) {
	fdo = stdout;
	copyFile(obj.wav[0].filename, fdo);
  } else if (obj.jobs > 1) {
	fdo = fopen(obj.wav[0].filename, "a");
  } else {
	goto exit0;
  }

  for (i=1; i<obj.jobs; i++) {
	copyFile(obj.wav[i].filename, fdo);
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


static int objList()
{
  int err = 0;  
  voice_t *list = NULL;
  int nbVoices = 0;
  int i;
  
  synthGetVoices(NULL, &nbVoices);
  if (err) {
	goto exit0;
  }
  list = (voice_t *)calloc(nbVoices, sizeof(voice_t));
  if (!list) {
	err = errno;
	goto exit0;
  }
  err = synthGetVoices(list, &nbVoices);
  if (err) {
	goto exit0;
  }

  printf("Name,Language,Variant\n");
  for (i=0; i<nbVoices; i++) {
	printf("%s,%s,%s\n", list[i].name, list[i].language, list[i].variant);
  }

exit0:
  if (list) {
	free(list);
  }
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
return err;
}


static int objSayText(region_t r, char* output, int withWavHeader)
{
  long length = 0;
  int err = 0;  
  
  if (!output) {
	err = EINVAL;
	goto exit0;
  }
  
  if (obj.wav[0].fd) {
	fclose(obj.wav[0].fd);
	obj.wav[0].fd = NULL;
  }
  
  if (obj.text.fd) {
	fclose(obj.text.fd);
	obj.text.fd = NULL;
  }

  obj.region.begin = r.begin;
  obj.region.end = r.end;
  obj.wav[0].filename = output;
  obj.withWavHeader = withWavHeader;
  
  msg("[%d] begin=%ld, end=%ld [%s] ", getpid(), r.begin, r.end, output);

  obj.text.fd = fopen(obj.text.filename, "r");    
  if (!obj.text.fd) {
	err = errno;
	goto exit0;
  }

  if (fseek(obj.text.fd, obj.region.begin, SEEK_SET) == -1) {
	err = errno;
	goto exit0;
  }

  obj.wav[0].fd = fopen(obj.wav[0].filename, "w");
  if (!obj.wav[0].fd) {
	err = errno;
	goto exit0;
  }
  if (obj.withWavHeader) {
	wav_header_t w;
	memset(&w, 0, sizeof(w));
	fwrite(&w, 1, sizeof(w), obj.wav[0].fd);
  }
  
  if (!obj.tts.handle) {
	obj.tts.handle = synthInit(&obj.tts, obj.wav[0].fd);
	if (!obj.tts.handle) {
	  err = EIO;
	  goto exit0;
	}
  }
  
  length = 1;
  while(length) {
	err = sentenceGet(obj.region, obj.text.fd, &length);
	if (err) {	  
	  break;
	}	
  	if (!length) {
  	  if (feof(obj.text.fd)) {
  		err=0;
  		break;
  	  } else if (ferror(obj.text.fd)) {
  		err("file error: %s", obj.text.filename);
		err = EIO;
  		break;
  	  }
  	} else {
	  synthSay(&obj.tts, tempbuf);
	}
	obj.region.begin += length;
  }

 exit0:
  if (err) {
	char *s = strerror(err);
	err("%s", s);
  }
  if (obj.wav[0].fd) {
	fclose(obj.wav[0].fd);
	obj.wav[0].fd = NULL;
  }  
  if (obj.text.fd) {
	fclose(obj.text.fd);
	obj.text.fd = NULL;
  }
  return err;
}

static int objSay()
{
  long partlen = 0;
  pid_t pid[MAX_JOBS];
  int i = 0;
  int err = 0;
  region_t r, r0;
  r.begin = r.end = 0;
  r0.begin = r0.end = 0;
    
  if (!obj.text.filename || !obj.jobs || (obj.jobs > MAX_JOBS) || !obj.wav[0].filename) {
	err = EINVAL;
	goto exit0;
  }
	
  if (obj.region.end < obj.jobs) {
	obj.jobs = 1;
  }
	  
  partlen = obj.region.end/obj.jobs;

  for (i=0; i<obj.jobs; i++) {
	const char* fmt = "%s.part%d.raw";
	r.begin = r.end;
	if (i == obj.jobs-1) {
	  r.end = obj.region.end;
	} else {
	  r.end = r.begin + partlen;
	  if (sentenceGetPosPrevious(&r, obj.text.fd) == -1) {
		err = EINVAL;
		goto exit0;
	  }
	}
	if (!i) {
	  r0.begin = 0;
	  r0.end = r.end;
	  continue;
	}
	if (obj.wav[i].filename) {
	  free(obj.wav[i].filename);
	}
	obj.wav[i].filename = (char*)malloc(strlen(obj.wav[0].filename) + strlen(fmt) + 10);
	if (!obj.wav[i].filename) {
	  err = errno;
	  goto exit0;
	}
	sprintf((char *)obj.wav[i].filename, fmt, obj.wav[0].filename, i);
	obj.wav[i].temporary = 1;
	pid[i] = fork();
	if (!pid[i]) {	  
	  err = objSayText(r, obj.wav[i].filename, 0);
	  exit(err);
	}	
	msg("[%d] child pid=%d, begin=%ld, end=%ld [%s] ", getpid(), pid[i], obj.region.begin, obj.region.end, (obj.wav[i].filename) ? obj.wav[i].filename : "null");	  
  }

  err = objSayText(r0, obj.wav[0].filename, obj.withWavHeader);

  for (i=1; i < obj.jobs; i++) {
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

static int objCreate(char *input, char *output, int withWavHeader, int jobs, int speed, char *voice)
{
  int err = 0;
  
  ENTER();

  obj.text.filename = input;
  obj.wav[0].filename = output;
  obj.withWavHeader = withWavHeader;
  obj.jobs = jobs;
  obj.tts.speed = speed;
  obj.tts.voiceID = VOICE_UNDEFINED;
  if (voice) {
	int voiceID;
	err = synthSearchVoice(voice, &voiceID);
	free(voice);
	if (err) {
	  goto exit0;
	}
	obj.tts.voiceID = voiceID;
  }
  
  err = checkOutput(&obj.wav[0].filename, &obj.wav[0].temporary);
  if (err) {
	goto exit0;
  }

  err = checkInput(&obj.text.filename, &obj.text.temporary);
  if (err) {
	goto exit0;
  }

  if (obj.text.filename) {
	struct stat statbuf;
	if (obj.text.fd) {
	  fclose(obj.text.fd);
	}
	obj.text.fd = fopen(obj.text.filename, "r");
	if (!obj.text.fd) {
	  err = errno;
	  goto exit0;
	}
	if (stat(obj.text.filename, &statbuf) == -1) {
	  err = errno;
	  goto exit0;	  
	}
	obj.region.begin = 0;
	obj.region.end = statbuf.st_size;
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
  
  if (obj.text.fd) {
	fclose(obj.text.fd);
	obj.text.fd = NULL;
  }
  if (obj.text.filename) {
	if (obj.text.temporary) {
	  unlink(obj.text.filename);
	}
	free(obj.text.filename);
	obj.text.filename = NULL;
  }
  
  for (i=0; i<MAX_JOBS; i++) {
	if (obj.wav[i].fd) {
	  fclose(obj.wav[i].fd);
	  obj.wav[i].fd = NULL;
	}
	if (obj.wav[i].filename) {
	  if (obj.wav[i].temporary) {
		unlink(obj.wav[i].filename);
	  }
	  free(obj.wav[i].filename);
	  obj.wav[i].filename = NULL;
	}
  }
  
  if (obj.tts.handle) {
	eciDelete(obj.tts.handle);
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
  char *voice = NULL;
  
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
	  if (voice) {
		free(voice);
	  }
	  voice = strdup(optarg);
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
	err = objList();
	goto exit0;
  }
  
  {
	const char *s =  (optind == argc-1) ? argv[optind] : NULL;  
	sentenceCreate(s);
  }
  
  if ((jobs <= 0) || (jobs > MAX_JOBS)) {
	err = EINVAL;
	err("jobs=%d (limit=1..%d)", obj.jobs, MAX_JOBS);
	goto exit0;
  }

  err = objCreate(input, output, 1, jobs, speed, voice);
  if (err) {
	usage();
	goto exit0;
  }

  objSay();
  
 exit0:
  objDelete();

  if (err) {
	char *s = strerror(err);
	err("%s", s);
	fprintf(stderr,"Error: %s\n", s);
  }
  
  return 0;
}

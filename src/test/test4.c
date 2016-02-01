#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <iconv.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "eci.h"

#define TEST_DBG "/tmp/test_libvoxin.dbg"

#define MAX_LANGUAGES 22

enum lang_t {ENGLISH, FRENCH, GERMAN, MAX_TESTED_LANG};
const char *pathname[MAX_TESTED_LANG] = {"/tmp/test_libvoxin_en.raw", "/tmp/test_libvoxin_fr.raw", "/tmp/test_libvoxin_de.raw"};
const int max_samples[MAX_TESTED_LANG] = {1024, 20000, 40000};
ECIHand handle[MAX_TESTED_LANG];
const enum ECILanguageDialect lang[MAX_TESTED_LANG] = {eciGeneralAmericanEnglish, eciStandardFrench, eciStandardGerman};

char *quote[] = {
  "So long as there shall exist, by virtue of law and custom, decrees of "
  "damnation pronounced by society, artificially creating hells amid the "
  "civilization of earth, and adding the element of human fate to divine "
  "destiny; so long as the three great problems of the century--the "
  "degradation of man through pauperism, the corruption of woman through "
  "hunger, the crippling of children through lack of light--are unsolved; "
  "so long as social asphyxia is possible in any part of the world;--in "
  "other words, and with a still wider significance, so long as ignorance "
  "and poverty exist on earth, books of the nature of Les Miserables cannot "
  "fail to be of use."
  " "
  "HAUTEVILLE HOUSE, 1862.",
  
  "PREMIÈRE PROMENADE. "
  "Me voici donc seul sur la terre, n'ayant plus de frère, de prochain, d'ami,"
  "de société que moi-même Le plus sociable et le plus aimant des humains en "
  "a été proscrit. Par un accord unanime ils ont cherché dans les raffinements"
  "de leur haine quel tourment pouvait être le plus cruel à mon âme sensible, "
  "et ils ont brisé violemment tous les liens qui m'attachaient à eux. J'aurais"
  "aimé les hommes en dépit d'eux-mêmes. Ils n'ont pu qu'en cessant de l'être "
  "se dérober à mon affection. Les voilà donc étrangers, inconnus, nuls enfin "
  "pour moi puisqu'ils l'ont voulu. Mais moi, détaché d'eux et de tout, que "
  "suis-je moi-même ? Voilà ce qui me reste à chercher.",
  "Als Zarathustra dreissig Jahr alt war, verliess er seine Heimat und"
  "den See seiner Heimat und ging in das Gebirge. Hier genoss er seines"
  "Geistes und seiner Einsamkeit und wurde dessen zehn Jahr nicht müde."
  "Endlich aber verwandelte sich sein Herz, - und eines Morgens stand"
  "er mit der Morgenröthe auf, trat vor die Sonne hin und sprach zu ihr"
  "also:"
  "Du grosses Gestirn! Was wäre dein Glück, wenn du nicht Die hättest,"
  "welchen du leuchtest!"
  "Zehn Jahre kamst du hier herauf zu meiner Höhle: du würdest deines"
  "Lichtes und dieses Weges satt geworden sein, ohne mich, meinen Adler"
  "und meine Schlange."
};

struct data_t {
  int fd;
  short *samples;
};

struct data_t data[MAX_TESTED_LANG];

enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  struct data_t *data = pData;

  if (Msg == eciWaveformBuffer)
    {
      ssize_t len = write(data->fd, data->samples, 2*lParam);
    }
  return eciDataProcessed;
}

int set_engine(int i)
{
  size_t utf8_size;
  size_t iso8859_1_size;
  char *iso8859_1_buf;
  char *buf;

  if (i>MAX_TESTED_LANG)
    return __LINE__;

  handle[i] = eciNewEx(lang[i]);
  if (!handle[i])
    return __LINE__;

  data[i].samples = calloc(1,2*max_samples[i]);
  if (!data[i].samples)
    return __LINE__;
  
  data[i].fd = creat(pathname[i], S_IRUSR|S_IWUSR);
  if (data[i].fd == -1)
    return __LINE__;

  eciRegisterCallback(handle[i], my_client_callback, &data[i]);
  
  if (eciSetOutputBuffer(handle[i], max_samples[i], data[i].samples) == ECIFalse)
    return __LINE__;


  utf8_size = strlen(quote[i]);
  iso8859_1_size = 2*strlen(quote[i]);
  iso8859_1_buf = calloc(1, iso8859_1_size);

  if (i != ENGLISH) {
    iconv_t cd = iconv_open("ISO8859-1", "UTF8");
    buf = iso8859_1_buf;
    iconv(cd, &quote[i], &utf8_size, &iso8859_1_buf, &iso8859_1_size);
  } else {
    buf = quote[i];
  }
  
  if (eciAddText(handle[i], buf) == ECIFalse)
    return __LINE__;
  
  if (eciSynthesize(handle[i]) == ECIFalse)
    return __LINE__;

  return 0;
}


int main(int argc, char **argv)
{
  uint8_t *buf;
  size_t len;
  int i;
  enum ECILanguageDialect Languages[MAX_LANGUAGES];
  int nbLanguages=MAX_LANGUAGES;
  enum state_t {IDLE, SPEAKING, OVER};
  int state[MAX_TESTED_LANG];
  int nbLanguagesOver;
  
  if (eciGetAvailableLanguages(Languages, &nbLanguages))
    return __LINE__;
  
  for (i=0; i<nbLanguages; i++) {
    switch (Languages[i]) {
    case eciGeneralAmericanEnglish:
      set_engine(ENGLISH);
      state[ENGLISH] = 0;
      break;
    case eciStandardFrench:
      set_engine(FRENCH);
      state[FRENCH] = 0;
      break;
    case eciStandardGerman:
      set_engine(GERMAN);
      state[GERMAN] = 0;
      break;
    default:
      fprintf(stderr,"not yet tested: %d\n", Languages[i]);
      break;
    }
  }

  #define ONE_MILLISECOND_IN_NANOSECOND 1000000 
  struct timespec req;
  req.tv_sec=0;
  req.tv_nsec=ONE_MILLISECOND_IN_NANOSECOND;

  nbLanguagesOver = 0;
  while(nbLanguagesOver != nbLanguages) {
    for (i=0; i<nbLanguages; i++) {
      switch (state[i]) {
      case IDLE:
	if (eciSpeaking(handle[i]) == ECITrue)
	  state[i] = SPEAKING;
	break;
      case SPEAKING:
      if (eciSpeaking(handle[i]) == ECIFalse) {
	state[i] = OVER;
	++nbLanguagesOver;
      }
	break;
      default:
	break;
      }
    }
    nanosleep(&req, NULL);
  }
  
  for (i=0; i<nbLanguages; i++) {
    switch (Languages[i]) {
    case eciGeneralAmericanEnglish:
    case eciStandardFrench:
      if (eciDelete(handle[i]) != NULL)
	return __LINE__;
    default:
      break;
    }
  }

 exit0:
  return 0;
}

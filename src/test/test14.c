/*
  3 languages, eciNewEx eci+nve
*/
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

#define nveEnglishNathan 0x2d0000
#define nveFrenchThomas 0x380000
#define nveSpanishMarisol 0x340000

enum lang_t {ENGLISH, FRENCH, GERMAN, SPANISH, MAX_TESTED_LANG};
const char *pathname[MAX_TESTED_LANG] = {"/tmp/test_libvoxin_en.raw", "/tmp/test_libvoxin_fr.raw", "/tmp/test_libvoxin_ge.raw", "/tmp/test_libvoxin_sp.raw"};
const int max_samples[MAX_TESTED_LANG] = {1024, 20000, 40000, 20000};
ECIHand handle[MAX_TESTED_LANG];
const enum ECILanguageDialect lang[MAX_TESTED_LANG] = {
	eciGeneralAmericanEnglish,
//	nveEnglishNathan,
	nveFrenchThomas,
	// eciStandardFrench,
	eciStandardGerman,
	nveSpanishMarisol,
	//eciCastilianSpanish,
};
const char * setVoiceCommand[MAX_TESTED_LANG] = {" `l1.0 `v2 ", " `l3.0 `v7 ", " `l4.0 `v2 "};
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
	"Me voici donc seul sur la terre, n'ayant plus de frère, de prochain, d'ami, "
	"de société que moi-même Le plus sociable et le plus aimant des humains en "
	"a été proscrit. Par un accord unanime ils ont cherché dans les raffinements "
	"de leur haine quel tourment pouvait être le plus cruel à mon âme sensible, "
	"et ils ont brisé violemment tous les liens qui m'attachaient à eux. J'aurais "
	"aimé les hommes en dépit d'eux-mêmes. Ils n'ont pu qu'en cessant de l'être "
	"se dérober à mon affection. Les voilà donc étrangers, inconnus, nuls enfin "
	"pour moi puisqu'ils l'ont voulu. Mais moi, détaché d'eux et de tout, que "
	"suis-je moi-même ? Voilà ce qui me reste à chercher.",
	
	"Als Zarathustra dreissig Jahr alt war, verliess er seine Heimat und "
	"den See seiner Heimat und ging in das Gebirge. Hier genoss er seines "
	"Geistes und seiner Einsamkeit und wurde dessen zehn Jahr nicht müde. "
	"Endlich aber verwandelte sich sein Herz, - und eines Morgens stand "
	"er mit der Morgenröthe auf, trat vor die Sonne hin und sprach zu ihr "
	"also: "
	"Du grosses Gestirn! Was wäre dein Glück, wenn du nicht Die hättest, "
	"welchen du leuchtest! "
	"Zehn Jahre kamst du hier herauf zu meiner Höhle: du würdest deines "
	"Lichtes und dieses Weges satt geworden sein, ohne mich, meinen Adler "
	"und meine Schlange.",

	"Yo, Juan Gallo de Andrada, escribano de Cámara del Rey nuestro "
	"señor, de los que residen en su Consejo, certifico y doy fe que, "
	"habiendo visto por los señores dél un libro intitulado El ingenioso "
	"hidalgo de la Mancha, compuesto por Miguel de Cervantes Saavedra, "
	"tasaron cada pliego del dicho libro a tres maravedís y medio; el cual "
	"tiene ochenta y tres pliegos, que al dicho precio monta el dicho libro "
	"docientos y noventa maravedís y medio, en que se ha de vender en "
	"papel; y dieron licencia para que a este precio se pueda vender, y "
	"mandaron que esta tasa se ponga al principio del dicho libro, y no se "
	"pueda vender sin ella. Y, para que dello conste, di la presente en "
	"Valladolid, a veinte días del mes de deciembre de mil y seiscientos y "
	"cuatro años."	
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
	char *buf;
	int res = 0;
  
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

	// TODO charset
	/* if (i != ENGLISH) { */
	/*   size_t utf8_size = strlen(quote[i]); */
	/*   size_t iso8859_1_size = 2*strlen(quote[i]); */
	/*   char *iso8859_1_buf = calloc(1, iso8859_1_size);     */
	/*   iconv_t cd = iconv_open("ISO8859-1", "UTF8"); */
	/*   buf = iso8859_1_buf; */
	/*   iconv(cd, &quote[i], &utf8_size, &iso8859_1_buf, &iso8859_1_size); */
	/*   iconv_close(cd); */
	/* } else { */
    buf = strdup(quote[i]);
	/* } */

	eciSetParam(handle[i], eciInputType, 1);
	eciSetVoiceParam(handle[i], 0, eciSpeed, 100);
#ifdef TODO
	eciAddText(handle[i], setVoiceCommand[i]);
#endif
	if (eciAddText(handle[i], buf) == ECIFalse) {
		res = __LINE__;
	}
  
	free(buf);
	return res;
}


int say_synchronous(int i) {
	int res = 0;
	if ((i >= MAX_TESTED_LANG) || !handle[i])
		return __LINE__;
  
	if (eciSynthesize(handle[i]) == ECIFalse) {
		res = __LINE__;	
	}
  
	if (eciSynchronize(handle[i]) == ECIFalse) // to removed to test eciSpeaking
		return __LINE__;
	//

	return 0;
}

int say_asynchronous() {
	int res = 0;
	int nbCompletedLanguages = 0;
	int nb_languages = 0;
	enum state_t {STATE_UNDEFINED, STATE_IDLE, STATE_SPEAKING, STATE_COMPLETED};
	int state[MAX_TESTED_LANG];
	int i;

	for (i=0; i<MAX_TESTED_LANG; i++) {
		if (handle[i]) {
			nb_languages++;
			if (eciSynthesize(handle[i]) == ECIFalse) {
				return __LINE__;	
			}

			state[i] = STATE_IDLE;
			fprintf(stderr, "State of 0x%08x: idle\n", lang[i]);										
		} else {
			state[i] = STATE_UNDEFINED;
		}
	}
	
#define ONE_MILLISECOND_IN_NANOSECOND 1000000 

	nbCompletedLanguages = 0;
	i = 0;
	while (nbCompletedLanguages != nb_languages) {
		struct timespec req = {.tv_sec=0, .tv_nsec=10*ONE_MILLISECOND_IN_NANOSECOND};
		i = (i+1) % MAX_TESTED_LANG;
		if (!handle[i])
			continue;
		switch (state[i]) {
		case STATE_IDLE:
			if (eciSpeaking(handle[i]) == ECITrue) {
				state[i] = STATE_SPEAKING;
				fprintf(stderr,"State of 0x%08x: speaking\n", lang[i]);				
			}
			break;
		case STATE_SPEAKING:
			if (eciSpeaking(handle[i]) == ECIFalse) {
				state[i] = STATE_COMPLETED;
				++nbCompletedLanguages;
				fprintf(stderr,"State of 0x%08x: completed\n", lang[i]);
			}
			break;
		default:
			break;
		}
		// req.tv_sec=0;
		// req.tv_nsec=10*ONE_MILLISECOND_IN_NANOSECOND;
		nanosleep(&req, NULL);
	}
	return 0;
}

int main(int argc, char **argv) {
	uint8_t *buf;
	size_t len;
	int i;
	enum ECILanguageDialect Languages[MAX_LANGUAGES];
	int nbLanguages=MAX_LANGUAGES;
	  
	if (eciGetAvailableLanguages(Languages, &nbLanguages))
		return __LINE__;

	for (i=0; i<nbLanguages; i++) {
		switch (Languages[i]) {
		case eciGeneralAmericanEnglish:
//		case nveEnglishNathan:
			set_engine(ENGLISH);
			break;
//		case eciStandardFrench:
		case nveFrenchThomas:
			set_engine(FRENCH);
			break;
			// case eciStandardGerman:
			//   set_engine(GERMAN);
			//   break;
//		case eciCastilianSpanish:
		case nveSpanishMarisol:
			set_engine(SPANISH);
			break;
		default:
			fprintf(stderr,"Voice not yet tested: 0x%x\n", Languages[i]);
			break;
		}
	}

	// for (i=0; i<nbLanguages; i++) {
	// 	say_synchronous(i);
	// }

	int res = say_asynchronous();
	if (res)
		return res;
	
  
	for (i=0; i<MAX_TESTED_LANG; i++) {
		if (!handle[i])
			continue;
		free(data[i].samples);
		close(data[i].fd);
		if (eciDelete(handle[i]) != NULL)
			return __LINE__;
	}

exit0:
	return 0;
}

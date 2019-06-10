#ifndef VOXIN_H
#define VOXIN_H

#include <stdint.h>
#include "eci.h"

typedef enum {voxFemale, voxMale} voxGender;
typedef enum {voxAdult, voxChild, voxSenior} voxAge;

#define VOX_STR_MAX 128
typedef struct {
  uint32_t id; // voice identifier
  char name[VOX_STR_MAX]; // optional: 'Yelda',...
  char lang[VOX_STR_MAX]; // ietf sub tag, iso639-1, 2 letters code: 'en', 'tr',...
  char variant[VOX_STR_MAX]; // ietf sub tag, optional: 'scotland', 'CA',...
  uint32_t rate; // sample rate in Hertz: 11025, 22050
  uint32_t  size; // sample size e.g. 16 bits
  /* chanels = 1 */
  /* encoding = signed-integer PCM */
  char charset[VOX_STR_MAX]; // "UTF-8", "ISO-8859-1",...
  voxGender gender;
  voxAge age;
  char multilang[VOX_STR_MAX]; // optional, e.g. "en,fr"
  char quality[VOX_STR_MAX]; // optional, e.g. "embedded-compact"
} vox_t;

// return 0 if ok
int voxGetVoices(vox_t *list, unsigned int *nbVoices);

// convert the vox_t data to a string in the buffer supplied by the caller (up to len, 0 terminator included)
// return 0 if ok
int voxString(vox_t *v, char *s, size_t len);

#endif


#ifndef TTS_H
#define TTS_H

#include <stdio.h>
#include "wavfile.h"

#define SPEED_UNDEFINED -1
#define MAX_CHAR 10240

void *ttsCreate(const char *voiceName, int speed);
void ttsDelete(void *handle);
int ttsSetVoice(void *handle, unsigned int id);
int ttsSetOutput(void *handle, void *wav, unsigned int part);
int ttsGetRate(void *handle);
int ttsSay(void *handle, const char *text);
int ttsPrintList(void *handle);

#endif

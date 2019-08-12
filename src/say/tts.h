#ifndef TTS_H
#define TTS_H

#include <stdio.h>

#define SPEED_UNDEFINED -1
#define MAX_CHAR 10240

void *ttsCreate(const char *voiceName, int speed);
void ttsDelete(void *handle);
int ttsSetVoice(void *handle, unsigned int id);
int ttsInit(void *handle, FILE *fdo);
int ttsGetRate(void *handle);
int ttsSay(void *handle, const char *text);
int ttsPrintList(void *handle);

#endif

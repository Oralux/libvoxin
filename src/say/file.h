#ifndef FILE_H
#define FILE_H

#include <stdbool.h>

typedef struct {
  long begin;
  long end;
} region_t;

void *fileCreate(const char *filename, bool writable);
int fileDelete(void *handle);
int fileSentenceGet(void *handle, region_t r, long *length);
int fileSentenceGetPosPrevious(void *handle, region_t *r);

#endif

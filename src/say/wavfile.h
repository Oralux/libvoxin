#ifndef WAVFILE_H
#define WAVFILE_H

#include <stdint.h>
#include <stdbool.h>

void *wavfileCreate(const char *filename, bool withHeader);
int wavfileDelete(void *handle);
int wavfileSetHeader(void *handle, uint32_t wavSize, uint32_t rate);
int wavfileGetDataSize(void *handle, size_t *wavSize);
int wavfileAppend(void *handleSrc, void *handleDest);
  
#endif

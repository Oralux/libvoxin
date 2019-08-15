#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define FILE_READABLE 1
#define FILE_WRITABLE 2
#define FILE_APPEND 4

typedef struct {
  char *filename;
  FILE *fd;
  size_t len; // data read or written according mode
  bool fifo; // is the standard input (textfile) or the standard output (wavfile) redirected to a pipe.
  int mode; // FILE_READABLE or FILE_WRITABLE, FILE_APPEND
  bool unlink;
} file_t;


file_t *fileCreate(const char *filename, int mode, bool fifo);
int fileDelete(file_t *self);
int fileRead(file_t *handle, uint8_t *data, size_t len);
int fileWrite(file_t *handle, uint8_t *data, size_t len);
int fileAppend(file_t *self, file_t *src);

#endif

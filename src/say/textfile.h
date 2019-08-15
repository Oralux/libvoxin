#ifndef TEXTFILE_H
#define TEXTFILE_H

typedef struct {
  long begin;
  long end;
} region_t;

void *textfileCreate(const char *filename);
int textfileDelete(void *handle);
int textfileSentenceGetPosPrevious(void *handle, region_t *r);

#endif

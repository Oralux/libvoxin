#ifndef TEXTFILE_H
#define TEXTFILE_H

void *textfileCreate(const char *inputfile, unsigned int *number_of_parts, const char *sentence);
int textfileDelete(void *handle);
int textfileGetNextSentences(void *handle, unsigned int part, long *length, const char **sentence);

#endif

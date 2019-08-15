#ifndef WAVFILE_H
#define WAVFILE_H

#include <stdint.h>
#include <stdbool.h>
/*
This file manage a wavfile potentially splitted in multiple parts.

The data are written in an internal buffer and finally flushed either to the output file initially supplied or stdout.
stdout is expected to be redirected to a file or a pipe.

*/


/* wavfileCreate is the first function to call.

ARGS
filename: where wavfileFlush will write the internal buffer
          if filename == NULL, stdout will be used instead. 

number_of_parts = 1 for a single file or > 1 to split the wafile in this number of parts.
rate : the speech rate in Hertz which will be copied in the header (e.g. rate = 11025)

*/

void *wavfileCreate(const char *output, size_t number_of_parts, uint32_t rate);

int wavfileDelete(void *handle);

/* wavfileWriteData writes yje data supplied to the corresponding part part =1 or greater)  */
int wavfileWriteData(void *handle, unsigned int part, uint8_t *data, size_t len);

/* wavfileFlush writes its internal buffer to the output */
int wavfileFlush(void *handle);
  
#endif

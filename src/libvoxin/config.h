#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "voxin.h"
#include "inote.h"

typedef struct {
  voxCapitalMode capital_mode;
  inote_punct_mode_t punctuation_mode;
  char *some_punctuation;
  char *filename;
} config_t;


#define CONFIG_DEFAULT (config_t){		\
    .capital_mode=voxCapitalNone,		\
      .punctuation_mode=INOTE_PUNCT_MODE_NONE,	\
      .some_punctuation="(),?",			\
};

typedef enum {
  CONFIG_OK=0,
  CONFIG_ARGS_ERROR,
  CONFIG_SYS_ERROR,
  CONFIG_FILE_ERROR,
  CONFIG_SYNTAX_ERROR,
} config_error;

config_error config_create(config_t **config);
config_error config_delete(config_t **config);

#endif

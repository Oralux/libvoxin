#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "voxin.h"
#include "inote.h"

typedef struct {
  char *dictionary_dir;
  bool use_abbreviation;
} config_eci_t;

typedef struct {
  voxCapitalMode capital_mode;
  inote_punct_mode_t punctuation_mode;
  char *some_punctuation;
  char *voice_name;
  char *filename;
  config_eci_t *eci;
} config_t;

typedef enum {
  CONFIG_OK=0,
  CONFIG_ARGS_ERROR,
  CONFIG_SYS_ERROR,
  CONFIG_FILE_ERROR,
  CONFIG_SYNTAX_ERROR,
} config_error;

/**
   @brief Create a config_t object according to a configuration file.

   If filename is null, the object is set to its default value.

   NOTES

   * If a string parameter, which can be set to any string, is let
   empty in the configuration file, its C value is set to an empty
   string.  

   e.g. this line in the configuration file:
   param=
   
   will give:
   config->param[0] == 0
   
   * If a parameter is set to an unexpected value, then this value is
   ignored.

   e.g. if capitalization expects "none", "icon", then this line is
   ignored:
   capitalization=

   * If a parameter is undefined or ignored in the configuration file,
   its corresponding C value is set to its default value.

   @param[out] config  pointer to the new config_t object
   @param[in] filename  configuration file to parse. If NULL, the default configuration is used
   @return config_error  CONFIG_OK on success
*/
config_error config_create(config_t **config, const char *filename);

/**
   @brief Delete a config_t object.

   @param[in,out] config  object to delete; sets *config to NULL on success
   @return config_error  CONFIG_OK on success
*/
config_error config_delete(config_t **config);

#endif

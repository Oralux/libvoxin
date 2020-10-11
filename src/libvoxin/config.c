#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "config.h"
#include "ini.h"
#include "debug.h"

#define CONFIG_FILE ".config/voxin/voxin.ini"

static int config_cb(void *user, const char *section, const char *name, const char *value) {
  config_t *conf = user;

  if (!user || !section || !name || !value) {
    err("args error");
    return 0;
  }

  dbg("[%s] %s=%s", section, name, value);
  
  if (!strcasecmp(section, "general")) {
    if (!strcasecmp(name, "capitalization")) {
      bool updated = true;
      if (!strcasecmp(value, "none")) {
	conf->capital_mode = voxCapitalNone;
      } else if (!strcasecmp(value, "icon")) {
	conf->capital_mode = voxCapitalSoundIcon;
      } else if (!strcasecmp(value, "spell")) {
	conf->capital_mode = voxCapitalSpell;
      } else if (!strcasecmp(value, "pitch")) {
	conf->capital_mode = voxCapitalPitch;
      } else {
	updated = false;
      }
      if (updated) {
	dbg("capital_mode=%d", conf->capital_mode);
      }
    } else if (!strcasecmp(name, "punctuation")) {
      bool updated = true;
      if (!strcasecmp(value, "none")) {
	conf->punctuation_mode = INOTE_PUNCT_MODE_NONE;
      } else if (!strcasecmp(value, "all")) {
	conf->punctuation_mode = INOTE_PUNCT_MODE_ALL;
      } else if (!strcasecmp(value, "some")) {
	conf->punctuation_mode = INOTE_PUNCT_MODE_SOME;
      } else {
	updated = false;
      }
      if (updated) {
	dbg("punctuation_mode=%d", conf->punctuation_mode);
      }
    }
  } else if (!strcasecmp(name, "somePunctuation")) {
    bool updated = false;
    if (conf->some_punctuation) {
      if (!value || strcmp(conf->some_punctuation, value)) {
	free(conf->some_punctuation);
	conf->some_punctuation = NULL;
	updated = true;
      }
    }
    if (value && !conf->some_punctuation) {
      conf->some_punctuation = strdup(value);
      updated = true;
    }
    if (updated) {
      dbg("some_punctuation=%s", conf->some_punctuation ? conf->some_punctuation : "NULL");
    }
  }
  return 1;
}

config_error config_create(config_t **config) {
  ENTER();
  char *home = getenv("HOME");
  config_t *conf = NULL;
  int err = CONFIG_OK;
  
  if (!home || !config)
    return CONFIG_ARGS_ERROR;

  conf = calloc(1, sizeof(*conf));
  if (!conf)
    return CONFIG_SYS_ERROR;

  *conf = CONFIG_DEFAULT;
  
  { // get filename
    char c[30];
    size_t size = 1 + snprintf(c, sizeof(c), "%s/%s", home, CONFIG_FILE);
    conf->filename = calloc(1, size);
    if (!conf->filename) {
      err = CONFIG_SYS_ERROR;
      goto exit0;
    }
    snprintf(conf->filename, size, "%s/%s", home, CONFIG_FILE);
  }    

  { // request inih to parse the voxin.ini configuration file
    int res = ini_parse(conf->filename, config_cb, conf);
    switch(res) {
    case 0:
      err = CONFIG_OK;
      break;
    case -1:
      dbg("file can't be opened: %s", conf->filename);
      err = CONFIG_FILE_ERROR;
      break;
    case -2:
      dbg("memory allocation error\n");
      err = CONFIG_SYS_ERROR;
      break;
    default:
      dbg("syntax error at line %d (%s)", res, conf->filename);
      err = CONFIG_SYNTAX_ERROR;
      break;
    }
  }
  
 exit0:
  if (err) {
    config_delete(&conf);
  } else {
    *config = conf;
  }
  return err;
}

config_error config_delete(config_t **config) {
  ENTER();
  config_t *conf;

  if (!config)
    return CONFIG_ARGS_ERROR;

  conf = *config;
  if (conf->filename) {
    free(conf->filename);
  }
  if (conf->some_punctuation) {
    free(conf->some_punctuation);
  }

  memset(conf, 0, sizeof(*conf));
  free(conf);
  *config = NULL;
  return CONFIG_OK;
}

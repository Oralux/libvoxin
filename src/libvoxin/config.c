#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "config.h"
#include "ini.h"
#include "debug.h"

#define SOME_DEFAULT_PUNCTUATION "(),?"
#define DEFAULT_ECI_DICTIONARY_DIR "/var/opt/IBM/ibmtts/dict"

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
    } else if (!strcasecmp(name, "somePunctuation")) {
      bool updated = false;
      if (conf->some_punctuation) {
	if (strcmp(conf->some_punctuation, value)) {
	  free(conf->some_punctuation);
	  conf->some_punctuation = NULL;
	  updated = true;
	}
      }
      if (!conf->some_punctuation) {
	conf->some_punctuation = strdup(value);
	updated = true;
      }
      if (updated) {
	dbg("some_punctuation=%s", conf->some_punctuation ? conf->some_punctuation : "NULL");
      }
    }
  } else if (!strcasecmp(section, "viavoice")) {
    config_eci_t *eci = conf->eci;
    if (!eci) {
      err("internal error");
    } else if (!strcasecmp(name, "dictionaryDir")) {
      bool updated = false;
      struct stat buf;
      if (eci->dictionary_dir
	  && strcmp(eci->dictionary_dir, value)) {
	if (!*value
	    || (!stat(value, &buf)
		&& ((buf.st_mode & S_IFMT) == S_IFDIR))) {
	  free(eci->dictionary_dir);
	  eci->dictionary_dir = NULL;
	  updated = true;
	} else {
	  // unexpected value: keep the default value
	  int err = errno; // ignore this value
	  dbg("erroneous dictionary dir, err=%s (%s)", strerror(err), value);
	}
      }
      if (!eci->dictionary_dir) {
	// note: value == valid dir path or *value == 0
	eci->dictionary_dir = strdup(value);
	updated = true;
	if (!eci->dictionary_dir) {
	  dbg("memory allocation error");    
	  return CONFIG_SYS_ERROR;
	}
      }
      if (updated) {
	dbg("viavoice.dictionaryDir=%s", eci->dictionary_dir ? eci->dictionary_dir : "NULL");
      }
    } else if (!strcasecmp(name, "useAbbreviation")) {
      bool updated = true;
      if (!strcasecmp(value, "yes")) {
	eci->use_abbreviation = true;
      } else if (!strcasecmp(value, "no")) {
	eci->use_abbreviation = false;
      } else {
	updated = false;
      }
      if (updated) {
	dbg("use_abbreviation=%d", eci->use_abbreviation);
      }
    }
  }
  
  return 1;
}

static config_error _config_eci_delete(config_eci_t **config) {
  ENTER();
  config_eci_t *conf;
  
  if (!config) {
    dbg("args error");    
    return CONFIG_ARGS_ERROR;
  }

  conf = *config;
  if (conf->dictionary_dir) {
    free(conf->dictionary_dir);
  }

  memset(conf, 0, sizeof(*conf));
  free(conf);
  *config = NULL;
  return CONFIG_OK;
}
 
static config_error _config_eci_create(config_eci_t **config) {
  ENTER();
  config_eci_t *conf = NULL;
  int err = CONFIG_OK;
  
  if (!config) {
    dbg("args error");    
    return CONFIG_ARGS_ERROR;
  }

  *config = NULL;
  
  conf = calloc(1, sizeof(*conf));
  if (!conf) {
    dbg("memory allocation error");    
    return CONFIG_SYS_ERROR;
  }

  conf->dictionary_dir = strdup(DEFAULT_ECI_DICTIONARY_DIR);
  if (!conf->dictionary_dir) {
    dbg("memory allocation error");    
    return CONFIG_SYS_ERROR;
  }
  dbg("viavoice.dictionaryDir=%s", conf->dictionary_dir);
  
  *config = conf;
  return err;
}

static config_error _config_get_default(config_t **config) {
  ENTER();
  config_t *conf = NULL;
  int err = CONFIG_OK;
  
  if (!config) {
    dbg("args error");    
    return CONFIG_ARGS_ERROR;
  }

  *config = NULL;
  
  conf = calloc(1, sizeof(*conf));
  if (!conf) {
    dbg("memory allocation error");    
    return CONFIG_SYS_ERROR;
  }

  *conf = (config_t) {
    .capital_mode = voxCapitalNone,
    .punctuation_mode = INOTE_PUNCT_MODE_NONE,
  };

  conf->some_punctuation = strdup(SOME_DEFAULT_PUNCTUATION);
  if (!conf->some_punctuation) {
    dbg("memory allocation error");    
    err = CONFIG_SYS_ERROR;
    goto exit0;
  }

  err = _config_eci_create(&conf->eci);

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
  config_error err = CONFIG_OK;
  
  if (!config)
    return CONFIG_ARGS_ERROR;
  
  conf = *config;
  if (conf->filename) {
    free(conf->filename);
    conf->filename = NULL;
  }
  if (conf->some_punctuation) {
    free(conf->some_punctuation);
    conf->some_punctuation = NULL;
  }
  if (conf->eci) {
    err = _config_eci_delete(&conf->eci);
  }

  if (!err) {
    memset(conf, 0, sizeof(*conf));
    free(conf);
    *config = NULL;
  }
  return err;
}

config_error config_create(config_t **config, const char *filename) {
  dbg("ENTER: filename=%s", filename ? filename : "NULL");
  config_t *conf = NULL;
  int err = CONFIG_OK;
  
  if (!config) {
    dbg("args error");
    return CONFIG_ARGS_ERROR;
  }

  err = _config_get_default(&conf);
  if (err)
    return err;

  if (filename) {
    conf->filename = strdup(filename);
  }

  if (conf->filename) { // request inih to parse the configuration file
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
      dbg("memory allocation error");
      err = CONFIG_SYS_ERROR;
      break;
    default:
      dbg("syntax error at line %d (%s)", res, conf->filename);
      err = CONFIG_SYNTAX_ERROR;
      break;
    }
  }
  
  if (err) {
    config_delete(&conf);
  } else {
    *config = conf;
  }
  return err;
}

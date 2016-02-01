#include "msg.h"

static const char *msg[] = {
  "undefined",
  "add_text",
  "clear_errors",
  "clear_input",
  "copy_voice",
  "delete",
  "delete_dict",
  "error_message",
  "get_available_languages",
  "get_default_param",
  "get_dict",
  "get_param",
  "get_voice_param",
  "insert_index",
  "load_dict",
  "new",
  "new_dict",
  "new_ex",
  "pause",
  "prog_status",
  "register_callback",
  "reset",
  "set_dict",
  "set_output_buffer",
  "set_output_device",
  "set_output_filename",
  "set_default_param",
  "set_param",
  "set_voice_param",
  "speaking",
  "stop",
  "synchronize",  
  "synthesize",
  "version",
  "cb_waveform_buffer",
  "cb_phoneme_buffer",
  "cb_index_reply",
  "cb_phoneme_index_reply",
  "cb_word_index_reply",
  "cb_string_index_reply",
  "cb_audio_index_reply",
  "cb_synthesis_break",  
  "max",
};

const char *msg_string(enum msg_type m)
{
  return ((m>=0) && (m<MSG_MAX)) ? msg[m] : NULL;  
}


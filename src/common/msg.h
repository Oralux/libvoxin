#ifndef _MSG_H
#define _MSG_H

#include <stdint.h>
#include <stddef.h>

typedef enum {MSG_TTS_UNDEFINED=0, MSG_TTS_ECI, MSG_TTS_NVE, MSG_TTS_MAX} msg_tts_id;
#define MSG_TO_APP_ID   0x110A0005
#define MSG_TO_ECI_ID   (MSG_TO_APP_ID+(MSG_TTS_ECI<<8))
#define MSG_TO_NVE_ID   (MSG_TO_APP_ID+(MSG_TTS_NVE<<8))

#define MSG_TTS_MASK (MSG_TO_ECI_ID^MSG_TO_NVE_ID)
// MSG_TTS converts a destination to its enum msg_tts_id
// For example, converts MSG_TO_ECI_ID to MSG_TTS_ECI

#define MSG_TTS(a) (((a)&MSG_TTS_MASK)>>8)
#define MSG_CHECK(a) ((MSG_TTS(a) >= 0) && (MSG_TTS(a) <= MSG_TTS_MAX))
const char *msg_tts_id_string(msg_tts_id id);

// MSG_DST converts for example MSG_TTS_ECI to MSG_TO_ECI_ID, its message destination id
#define MSG_DST(a)  (MSG_TO_APP_ID+((a)<<8))

enum msg_type {
  MSG_UNDEFINED,
  MSG_ADD_TEXT,
  MSG_CLEAR_ERRORS,
  MSG_CLEAR_INPUT,
  MSG_COPY_VOICE,
  MSG_DELETE,
  MSG_DELETE_DICT,
  MSG_ERROR_MESSAGE,
  MSG_GET_AVAILABLE_LANGUAGES,
  MSG_GET_DEFAULT_PARAM,
  MSG_GET_DICT,
  MSG_GET_PARAM,
  MSG_GET_VOICE_PARAM,
  MSG_INSERT_INDEX,
  MSG_LOAD_DICT,
  MSG_NEW,
  MSG_NEW_DICT,
  MSG_NEW_EX,
  MSG_PAUSE,
  MSG_PROG_STATUS,
  MSG_REGISTER_CALLBACK,
  MSG_RESET,
  MSG_SET_DICT,
  MSG_SET_OUTPUT_BUFFER,
  MSG_SET_OUTPUT_DEVICE,
  MSG_SET_OUTPUT_FILENAME,
  MSG_SET_DEFAULT_PARAM,
  MSG_SET_PARAM,
  MSG_SET_VOICE_PARAM,
  MSG_SPEAKING,
  MSG_STOP,
  MSG_SYNCHRONIZE,
  MSG_SYNTHESIZE,
  MSG_VERSION,
  MSG_CB_WAVEFORM_BUFFER, //same order below than in ECIMessage
  MSG_CB_PHONEME_BUFFER,
  MSG_CB_INDEX_REPLY,
  MSG_CB_PHONEME_INDEX_REPLY,
  MSG_CB_WORD_INDEX_REPLY,
  MSG_CB_STRING_INDEX_REPLY,
  MSG_CB_AUDIO_INDEX_REPLY,
  MSG_CB_SYNTHESIS_BREAK,
  MSG_EXIT,
  MSG_ADD_TLV,
  MSG_VOX_GET_VOICES,
  MSG_MAX
};

#define MSG_LANG_INFO_MAX 22
struct msg_get_available_languages_t {
  uint32_t nb;
  uint32_t languages[MSG_LANG_INFO_MAX];
} __attribute__ ((packed));

#define MSG_VOX_STR_MAX 128
struct msg_vox_t {
  uint32_t id;
  char name[MSG_VOX_STR_MAX];
  char lang[MSG_VOX_STR_MAX];
  char variant[MSG_VOX_STR_MAX];
  uint32_t rate;
  uint32_t  size;
  char charset[MSG_VOX_STR_MAX];
  uint32_t gender;
  uint32_t age;
  char multilang[MSG_VOX_STR_MAX];
  char quality[MSG_VOX_STR_MAX];
} __attribute__ ((packed));

#define MSG_VOX_LIST_MAX 30
struct msg_vox_get_voices_t {
  uint32_t nb;
  struct msg_vox_t voices[MSG_VOX_LIST_MAX];
} __attribute__ ((packed));

struct msg_set_param_t {
  uint32_t Param;
  uint32_t iValue;
} __attribute__ ((packed));

struct msg_get_param_t {
  uint32_t Param;
} __attribute__ ((packed));

struct msg_get_voice_param_t {
  uint32_t iVoice;
  uint32_t Param;
} __attribute__ ((packed));

struct msg_set_voice_param_t {
  uint32_t iVoice;
  uint32_t Param;
  uint32_t iValue;
} __attribute__ ((packed));

struct msg_set_output_buffer_t {
  uint32_t nb_samples;
} __attribute__ ((packed));

struct msg_set_output_device_t {
  uint32_t iDevNum;
} __attribute__ ((packed));

struct msg_register_callback_t {
  uint32_t Callback; // clear callback if NULL
} __attribute__ ((packed));

struct msg_new_ex_t {
  uint32_t Value;
} __attribute__ ((packed));

struct msg_pause_t {
  uint32_t On;
} __attribute__ ((packed));

struct msg_insert_index_t {
  uint32_t iIndex;
} __attribute__ ((packed));

struct msg_copy_voice_t {
  uint32_t iVoiceFrom;
  uint32_t iVoiceTo;
} __attribute__ ((packed));

struct msg_set_dict_t {
  uint32_t hDict;
} __attribute__ ((packed));

struct msg_delete_dict_t {
  uint32_t hDict;
} __attribute__ ((packed));

struct msg_load_dict_t {
  uint32_t hDict;
  uint32_t DictVol;
} __attribute__ ((packed));

struct msg_callback_t {
  uint32_t lParam;
} __attribute__ ((packed));

union args_t {
  struct msg_get_param_t gp;
  struct msg_set_param_t sp;
  struct msg_get_voice_param_t gvp;
  struct msg_set_voice_param_t svp;
  struct msg_set_output_buffer_t sob;
  struct msg_set_output_device_t sod;
  struct msg_register_callback_t rc;
  struct msg_new_ex_t ne;
  struct msg_pause_t p;
  struct msg_insert_index_t ii;
  struct msg_copy_voice_t cv;
  struct msg_set_dict_t sd;
  struct msg_delete_dict_t dd;
  struct msg_load_dict_t ld;
  struct msg_callback_t cb;
} __attribute__ ((packed));

struct msg_t {
  uint32_t id;
  uint32_t count; // nb of msg sent
  uint32_t func; // func id from msg_type
  uint32_t engine;
  union args_t args;
  uint32_t res;
  uint32_t allocated_data_length;
  uint32_t effective_data_length;
  uint8_t data[0] __attribute__ ((aligned (16))); // placeholder
} __attribute__ ((packed, aligned (16)));

#define MSG_HEADER_LENGTH sizeof(struct msg_t)
#define MIN_MSG_SIZE MSG_HEADER_LENGTH
#define MAX_ERROR_MESSAGE 100
#define MAX_VERSION 20

extern const char *msg_string(enum msg_type m);

// generic type: bytes
struct msg_bytes_t {
  uint8_t *b;
  size_t len;
};
#define min_size(a,b) ((a<b)?a:b)	

#endif // _MSG_H

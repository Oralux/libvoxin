#ifndef LIBVOXIN_H
#define LIBVOXIN_H

#include "pipe.h"
#include "msg.h"

#define LIBVOXIN_ID 0x010A0005

typedef enum {VOX_TTS_ECI, VOX_TTS_NVE, VOX_TTS_MAX} vox_tts_id;

extern void *libvoxin_create();
extern int libvoxin_list_tts(void *handle, vox_tts_id *id, size_t *len);
extern int libvoxin_call_eci(void *handle, struct msg_t *msg);
extern void libvoxin_delete(void *handle);

#endif

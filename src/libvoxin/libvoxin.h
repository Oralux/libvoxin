#ifndef LIBVOXIN_H
#define LIBVOXIN_H

#include "pipe.h"
#include "msg.h"

#define LIBVOXIN_ID 0x010A0005

extern void *libvoxin_create();
extern int libvoxin_list_tts(void *handle, msg_tts_id *id, size_t *len);
extern int libvoxin_call_eci(void *handle, struct msg_t *msg);
extern void libvoxin_delete(void *handle);
extern const char *libvoxin_get_rootdir(void *handle);

#endif

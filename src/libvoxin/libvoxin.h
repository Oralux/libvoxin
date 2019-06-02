#ifndef LIBVOXIN_H
#define LIBVOXIN_H

#include "pipe.h"
#include "msg.h"

#define LIBVOXIN_ID 0x010A0005

typedef void *libvoxin_handle_t;

extern int libvoxin_create(libvoxin_handle_t *i, uint32_t with_eci);
extern int libvoxin_call_eci(libvoxin_handle_t i, struct msg_t *msg);
extern int libvoxin_delete(libvoxin_handle_t *i);

#endif

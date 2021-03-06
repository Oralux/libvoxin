#ifndef __DEBUG_H_
#define __DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "msg.h"


  // log enabled if this file exits under $HOME
#define ENABLE_LOG "libvoxin.ok"

  // log level; first byte equals to a digit in DebugLevel (default 
#define LIBVOXINLOG "/tmp/libvoxin.log.%lu"

  enum libvoxinDebugLevel {LV_ERROR_LEVEL=0, LV_INFO_LEVEL=1, LV_DEBUG_LEVEL=2, LV_LOG_DEFAULT=LV_ERROR_LEVEL};

#define log(level,fmt,...) if (libvoxinDebugEnabled(level)) {libvoxinDebugDisplayTime(); fprintf (libvoxinDebugFile, "%s: " fmt "\n", __func__, ##__VA_ARGS__);}
#define err(fmt,...) log(LV_ERROR_LEVEL, fmt, ##__VA_ARGS__)
#define msg(fmt,...) log(LV_INFO_LEVEL, fmt, ##__VA_ARGS__)
#define dbg(fmt,...) log(LV_DEBUG_LEVEL, fmt, ##__VA_ARGS__)
#define dbgText(text, len) if (libvoxinDebugEnabled(LV_DEBUG_LEVEL)) {libvoxinDebugTextWrite(text, len);}

#define ENTER() dbg("ENTER")
#define LEAVE() dbg("LEAVE")

  // compilation error if condition is not fullfilled (inspired from
  // BUILD_BUG_ON, linux kernel).
#define BUILD_ASSERT(condition) ((void)sizeof(char[(condition)?1:-1]))

  extern unsigned long libvoxinDebugGetTid();
  extern int libvoxinDebugEnabled(enum libvoxinDebugLevel level);
  extern void libvoxinDebugDisplayTime();
  extern size_t libvoxinDebugTextWrite(const char *text, size_t len);
  extern void libvoxinDebugDump(const char *label, const uint8_t *buf, size_t size);
  extern void libvoxinDebugFinish();
  extern FILE *libvoxinDebugFile;  
  extern FILE *libvoxinDebugText;  

#ifdef __cplusplus
}
#endif

#endif


#ifndef _CONF_H
#define _CONF_H

#ifdef WITH_RFS_32
#define VOXIND_CMD_ARG0 "/opt/voxin/usr/lib/ld-linux.so.2"
#define VOXIND_CMD_ARG1 "--inhibit-cache"
#define VOXIND_CMD_ARG2 "--library-path"
#define VOXIND_CMD_ARG3 "/opt/voxin/usr/lib"
#define VOXIND_CMD_ARG4 "/opt/voxin/usr/bin/voxind"
#else
#define VOXIND_CMD_ARG0 "/home/disk7/gcasse/VOXIN/LIBVOXIN/libvoxin/build/rfs/usr/bin/voxind"
#define VOXIND_CMD_ARG1 NULL
#define VOXIND_CMD_ARG2 NULL
#define VOXIND_CMD_ARG3 NULL
#define VOXIND_CMD_ARG4 NULL
#endif

#endif // _CONF_H

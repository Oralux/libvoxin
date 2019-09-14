#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>
#include "libvoxin.h"
#include "msg.h"
#include "debug.h"

#define IBMTTS_RO "opt/IBM/ibmtts"
#define IBMTTS_CFG "var/opt/IBM/ibmtts/cfg"
#define VOXIN_DIR "/opt/oralux/voxin"
#define RFS32 VOXIN_DIR "/rfs32"
// VOXIND: relative path when current working directory is RFS32
#define VOXIND "usr/bin/voxind"
// VOXIND_NVE: relative path when current working directory is VOXIN_DIR
#define VOXIND_NVE "bin/voxind-nve"
#define MAXBUF 4096

typedef struct {
  vox_tts_id id;
  char rootdir[MAXBUF]; // path to the root directory
  // For example, rootdir could be "/", "/opt/oralux/voxin/rfs32",
  // "/home/user1/.oralux/voxin/rfs",
  // "/home/user1/.oralux/voxin/rfs/opt/oralux/voxin/rfs32"
  char dir[MAXBUF]; // directory under which the voxind binary is expected (relative to rootdir)
  char bin[MAXBUF]; // path to the voxind binary (relative to rootdir/dir)
  char lib[MAXBUF]; // LD_LIBRARY_PATH if needed by voxind
  pid_t parent; // pid of the process which created voxind
  pid_t child; // pid of voxind
  struct pipe_t *pipe; // bi-directionnal pipe between the parent and the child processes
} voxind_t;

typedef struct {
  uint32_t id;
  uint32_t msg_count;
  pid_t parent;
  voxind_t *voxind[VOX_TTS_MAX];
  uint32_t stop_required;
  char rootdir[MAXBUF]; // path to the root directory, dynamically determined
} libvoxin_t;

static voxind_t voxind_init[VOX_TTS_MAX] = {
  {id:VOX_TTS_ECI, dir:RFS32, bin:VOXIND},
  {id:VOX_TTS_NVE, dir:VOXIN_DIR, bin:VOXIND_NVE}
};

static int voxind_write(voxind_t *self, void *buf, ssize_t *len) {
  ENTER();
  if (!self)
	return 0;
  return pipe_write(self->pipe, buf, len);
}

static void my_exit(voxind_t *self) {
  struct msg_t msg;
  ssize_t l = sizeof(struct msg_t);
  int res;
  
  memset(&msg, 0, MIN_MSG_SIZE);
  msg.id = MSG_EXIT;
  
  dbg("send exit msg");
  res = pipe_write(self->pipe, &msg, &l);
  if (res) {
	dbg("write error (%d)", res);
  }
}

// fdwalk function from glib (GPLv2):
// https://git.gnome.org/browse/glib/tree/glib/gspawn.c?h=2.50.0#n1027
static int fdwalk (int (*cb)(void *data, int fd), void *data) {
  int open_max;
  int fd;
  int res = 0;  
  struct rlimit rl;
  DIR *d;

  if ((d = opendir("/proc/self/fd"))) {
	struct dirent *de;

	while ((de = readdir(d))) {
	  long int l;
	  char *e = NULL;

	  if (de->d_name[0] == '.')
		continue;
            
	  errno = 0;
	  l = strtol(de->d_name, &e, 10);
	  if (errno != 0 || !e || *e)
		continue;

	  fd = (int) l;

	  if ((long int) fd != l)
		continue;

	  if (fd == dirfd(d))
		continue;

	  if ((res = cb (data, fd)) != 0)
		break;
	}
      
	closedir(d);
	return res;
  }

  /* If /proc is not mounted or not accessible we fall back to the old
   * rlimit trick */
  
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_max != RLIM_INFINITY)
	open_max = rl.rlim_max;
  else
	open_max = sysconf (_SC_OPEN_MAX);

  for (fd = 0; fd < open_max; fd++)
	if ((res = cb (data, fd)) != 0)
	  break;

  return res;
}


// Return the root directory of the absolute pathname of libvoxin
// linked to the running process.
// For example, if the running process is linked to
// /home/user1/.oralux/voxin/rfs/opt/oralux/voxin-2.00/lib/libvoxin.so.2.0.0
// then the returned dir is
// /home/user1/.oralux/voxin/rfs
//
static int get_root_dir(char *dir, size_t len) {
  FILE *fd = NULL;
  char *basename = NULL;
  int err = EINVAL;

  if (!dir || (len < MAXBUF))
	return EINVAL;

  *dir = 0;
  fd = fopen("/proc/self/maps", "r");
  if (!fd) {
	err = errno;
	dbg("%s\n", strerror(err));
	goto exit0;
  }

  while(1) {
	char *s = fgets(dir, len, fd);
	char *libname = NULL;
	if (!s) {
	  dbg("Can't read maps\n");
	  goto exit0;
	}

	dbg("/proc/self/maps: %s\n", s);
	// $rfs/opt/oralux/voxin-$ver/lib/libvoxin.so.$ver
	basename = strrchr(s, '/');
	if (!basename || (strncmp(basename+1, "libvoxin.so.", strlen("libvoxin.so.")))) {
	  dbg("not match (1)\n");
	  continue;
	}
	libname = basename+1;
	libname[strlen(libname)-1] = 0; // remove last char (cr)
	*basename = 0;

	{
	  const char *path=VOXIN_DIR "/lib";
	  size_t len = strlen(s);
	  size_t ref = strlen(path);
	  dbg("len=%lu, ref=%lu\n", (long unsigned int)len, (long unsigned int)ref);
	  dbg("s='%s'\n", s);
	  dbg("path='%s'\n", path);
	  if (len < ref) {
		dbg("not match (2)\n");
		continue;
	  }
	  if (len == ref) {
		strcpy(dir, "/");
	  } else {
		s[len-ref+1] = 0;
		basename = strchr(s, '/');
		if (!basename) {
		  dbg("not match (3)\n");
		  continue;
		}
		bcopy(basename, dir, strlen(basename)+1);
	  }
	  err = 0;
	  dbg("dir=%s\n", dir);
	  break;
	}	  
  }
	
 exit0:
  if (fd)
	fclose(fd);
  return err;
}
  

static int close_cb(void *data, int fd) {
  int res = 0;
  if (((void*)fd != data) && (close(fd) == -1))
	res = errno;
  return res;
}


static int voxind_routine(voxind_t *self) {
  int res = 0;
	
  ENTER();

  if (chdir(self->rootdir)) {
	res = errno;
	goto exit;
  }

  libvoxinDebugFinish();
  close(PIPE_COMMAND_FILENO);
  pipe_dup2(self->pipe, PIPE_SOCKET_CHILD_INDEX, PIPE_COMMAND_FILENO);
  fdwalk (close_cb, (void*)PIPE_COMMAND_FILENO);
  
  if (self->lib[0] && setenv("LD_LIBRARY_PATH", self->lib, 1)) {
	res = errno;
	goto exit;
  }

  if (execl(self->bin, self->bin, NULL) == -1) {
	res = errno;
  }

  my_exit(self);
  
 exit:
  if (res)
	dbg("%s\n", strerror(res));

  LEAVE();
  return res;  
}


static int voxind_start(voxind_t *self) {
  int err = 0;

  ENTER();
  
  if (!self)
	return EINVAL;

  self->child = fork();
  switch(self->child) {
  case 0:
	if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
	  exit(errno);
	  }
	if (getppid() != self->parent) {
	  exit(0);
	}
	  
	pipe_close(self->pipe, PIPE_SOCKET_PARENT);
	exit(voxind_routine(self));	
	break;
  case -1:
	err = errno;
	break;
  default:
	pipe_close(self->pipe, PIPE_SOCKET_CHILD_INDEX);
	break;
  }
  
  LEAVE();
  return err;
}


static int voxind_stop(voxind_t *self) {  
  ENTER();
  if (!self)
	return EINVAL;

  if (!self->child) { // TODO
  }
  
  LEAVE();
  return 0;
}

static void voxind_delete(voxind_t *self) {
  ENTER();
  if (self) {
	voxind_stop(self);
	pipe_delete(&self->pipe);
	// TODO stop thread
  }
}

static voxind_t *voxind_create(vox_tts_id id, char *rootdir) {
  ENTER();

  if ((id<0) || (id>=VOX_TTS_MAX) || !rootdir)
	return NULL;
  
  voxind_t *self = calloc(1, sizeof(*self));
  if (!self)
	return NULL;

  self->id = id;
  
  const size_t max = sizeof(self->rootdir);
  size_t len = strnlen(rootdir, max);
  if (len+1 >= max)
	goto exit;

  strncpy(self->rootdir, rootdir, len);
  
  // if id == VOX_TTS_NVE: rootdir unchanged and ld_library_path unset
  if (self->id == VOX_TTS_ECI) {
	// the root directory is RFS32
	// Note: eci.ini is expected to be accessible in the current
	// working directory
	//
	if (len + strlen(RFS32) >= max)
	  goto exit;
	
	strncpy(self->rootdir+len, RFS32, max-len);
	
	// LD_LIBRARY_PATH
	// e.g if voxinRoot = "/"
	// libraryPath = "/opt/IBM/ibmtts/lib:/opt/oralux/voxin/rfs32/lib:/opt/oralux/voxin/rfs32/usr/lib"
	const size_t max = sizeof(self->lib);
	len = snprintf(self->lib,
				   max,
				   "%s/%s/lib:%s" VOXIN_DIR "/rfs32/lib:%s" VOXIN_DIR "/rfs32/usr/lib",
				   self->rootdir, IBMTTS_RO,
				   self->rootdir, self->rootdir);
	if (len >= max) {
	  dbg("path too long\n");
	  goto exit;
	}
  }

  // is the binary file present?
  char buf[MAXBUF];
  len = snprintf(buf, sizeof(buf), "%s/%s/%s", self->rootdir, self->dir, self->bin);
  if (len >= sizeof(buf))
	goto exit;
  
  FILE *fd = fopen(buf, "r");
  if (fd)
	fclose(fd);

  if (!fd) {
	dbg("File not found %s", buf);
	goto exit;
  }

  dbg("%s: dir=%s, bin=%s, rootdir=%s, lib=%s",
	  (self->id == VOX_TTS_ECI) ? "eci" : "nve",
	  self->dir, self->bin, self->rootdir, self->lib);

  self->parent = getpid();

  int err = pipe_create(&self->pipe);  
  if (err)    
	goto exit;
  
  return self;
  
 exit:
  if (self) {
	voxind_delete(self);
	free(self);
  }
  return NULL;
}

void libvoxin_delete(void *handle) {
  libvoxin_t **pself = NULL;
  libvoxin_t *self = NULL;
  
  ENTER();

  if (!handle)
	return;

  pself = (libvoxin_t**)handle;  
  self = *pself;
  
  if (!self)
	return;
  
  int i;
  for (i=0; i<VOX_TTS_MAX; i++) {
	voxind_delete(self->voxind[i]);
  }

  memset(self, 0, sizeof(*self));
  free(self);
  *pself = NULL;

  LEAVE();
}

void *libvoxin_create() {
  static bool once = false;
  int err = 0;
  libvoxin_t *self = NULL;

  ENTER();

  if (once) {
	err = EINVAL;
	goto exit0;
  }

  self = (libvoxin_t*)calloc(1, sizeof(libvoxin_t));
  if (!self) {
	err = errno;
	goto exit0;
  }

  self->id = LIBVOXIN_ID;

  err = get_root_dir(self->rootdir, sizeof(self->rootdir));
  if (err) {
	goto exit0;
  }
  
  int i;
  for (i=0; i<VOX_TTS_MAX; i++) {  
	self->voxind[i] = voxind_create(i, self->rootdir);
	if (self->voxind[i])
	  voxind_start(self->voxind[i]);
  }
  
 exit0:
  if (err) {
	libvoxin_delete(&self);
	err("%s",strerror(err));
  } else {
	once = true;
  }

  LEAVE();  
  return self;
}


int libvoxin_list_tts(void* handle, vox_tts_id *id, size_t *len) {
  ENTER();
  libvoxin_t *self = (libvoxin_t *)handle;

  if (!self || !len) {
	err("LEAVE, args error(%d)",0);
	return EINVAL;
  }

  if (!id) {
	*len = 0;
	int i;
	for (i=0; i<VOX_TTS_MAX; i++) {
	  if (self->voxind[i])
		*len += 1;
	}
	dbg("len = %lu", (long unsigned int)*len);
	return 0;
  }

  const size_t max = (*len<VOX_TTS_MAX) ? *len : VOX_TTS_MAX;
  *len = 0;
  int i;
  for (i=0; i<max; i++) {
	if (self->voxind[i]) {
	  id[*len] = self->voxind[i]->id;
	  *len += 1;
	}
  }
  
  return 0;
}

int libvoxin_call_eci(void* handle, struct msg_t *msg) {
  int res;
  libvoxin_t *self = (libvoxin_t *)handle;
  size_t allocated_msg_length;
  size_t effective_msg_length;

  if (!self || !msg || (msg->id != MSG_TO_ECI_ID)) {
	err("LEAVE, args error(%d)",0);
	return EINVAL;
  }

  allocated_msg_length = MSG_HEADER_LENGTH + msg->allocated_data_length;
  effective_msg_length = MSG_HEADER_LENGTH + msg->effective_data_length;

  if ((effective_msg_length > allocated_msg_length) || !msg_string((enum msg_type)msg->func)){
	err("LEAVE, args error(%d)",1);
	return EINVAL;
  }

  msg->count = ++self->msg_count;
  dbg("send msg '%s', length=%d (#%d)",msg_string((enum msg_type)(msg->func)), msg->effective_data_length, msg->count);  
  ssize_t s = effective_msg_length;
  res = self->voxind_write(self->pipe, msg, &s);
  if (res)
	goto exit0;

  effective_msg_length = allocated_msg_length;
  memset(msg, 0, MSG_HEADER_LENGTH);
  s = effective_msg_length;
  res = self->_pipe_read(self->pipe, msg, &s);
  if (!res && (s >= 0)) {
	effective_msg_length = (size_t)s;
	if (!msg_string((enum msg_type)(msg->func))
		|| (effective_msg_length < MSG_HEADER_LENGTH + msg->effective_data_length)) {
	  res = EIO;
	} else if (msg->id == MSG_EXIT) {
	  dbg("recv msg exit");
	  // if the child process exits then libvoxin currently exits too.
	  sleep(1);
	  exit(1);
	  //      res = ECHILD;
	} else {
	  dbg("recv msg '%s', length=%d, res=0x%x (#%d)", msg_string((enum msg_type)(msg->func)), msg->effective_data_length, msg->res, msg->count);
	}
  }
  
 exit0:
  dbg("LEAVE(res=0x%x)",res);  
  return res;
}


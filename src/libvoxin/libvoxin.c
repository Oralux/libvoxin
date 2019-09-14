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
#include "no_pipe.h"
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

typedef enum {ID_ECI, ID_NVE} voxind_id; // index of the voxind array

typedef int (*t_pipe_create)(struct pipe_t **px);
typedef int (*t_pipe_delete)(struct pipe_t **px);
typedef int (*t_pipe_restore)(struct pipe_t **px, int fd);
typedef int (*t_pipe_dup2)(struct pipe_t *p, int index, int new_fd);
typedef int (*t_pipe_close)(struct pipe_t *p, int index);
typedef int (*t_pipe_read)(struct pipe_t *p, void *buf, ssize_t *len);
typedef int (*t_pipe_write)(struct pipe_t *p, void *buf, ssize_t *len);

struct libvoxin_t {
  uint32_t id;
  uint32_t msg_count;
  pid_t parent;
  pid_t child;
  struct pipe_t *pipe_command;
  uint32_t stop_required;
  t_pipe_create _pipe_create;
  t_pipe_delete _pipe_delete;
  t_pipe_restore _pipe_restore;
  t_pipe_dup2 _pipe_dup2;
  t_pipe_close _pipe_close;
  t_pipe_read _pipe_read;
  t_pipe_write _pipe_write;
  uint32_t with_eci;
};

typedef struct {
  voxind_id id;
  const char *dir; // directory under which the voxind binary is expected (relative to rootdir)
  const char *bin; // path to the voxind binary (relative to rootdir/dir)
  char rootdir[MAXBUF]; // path to the root directory, dynamically determined
  // For example, rootdir could be "/", "/opt/oralux/voxin/rfs32",
  // "/home/user1/.oralux/voxin/rfs",
  // "/home/user1/.oralux/voxin/rfs/opt/oralux/voxin/rfs32"
  char lib[MAXBUF]; // LD_LIBRARY_PATH (for eci), dynamically determined
  bool present; // true if binary found
} voxind_t;

static voxind_t voxind[2] = {
  {id:ID_ECI, dir:RFS32, bin:VOXIND},
  {id:ID_NVE, dir:VOXIN_DIR, bin:VOXIND_NVE}
};

static void my_exit(struct libvoxin_t *the_obj)
{
  struct msg_t msg;
  ssize_t l = sizeof(struct msg_t);
  int res;
  
  memset(&msg, 0, MIN_MSG_SIZE);
  msg.id = MSG_EXIT;
  
  dbg("send exit msg");
  res = the_obj->_pipe_write(the_obj->pipe_command, &msg, &l);
  if (res) {
	dbg("write error (%d)", res);
  }
}

// fdwalk function from glib (GPLv2):
// https://git.gnome.org/browse/glib/tree/glib/gspawn.c?h=2.50.0#n1027
static int fdwalk (int (*cb)(void *data, int fd), void *data)
{
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


// Return the absolute pathname of libvoxin linked to the running process
// For example, if the running process is linked to
// /home/user1/.oralux/voxin/rfs/opt/oralux/voxin-2.00/lib/libvoxin.so.2.0.0
// then the returned dir is
// /home/user1/.oralux/voxin/rfs
//
static int getVoxinRootDir(char *dir, size_t len) {
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


static int child(struct libvoxin_t *the_obj, voxind_t *v)
{
  int res = 0;
	
  ENTER();

  if (chdir(v->rootdir)) {
	res = errno;
	goto exit;
  }

  libvoxinDebugFinish();
  close(PIPE_COMMAND_FILENO);
  the_obj->_pipe_dup2(the_obj->pipe_command, PIPE_SOCKET_CHILD_INDEX, PIPE_COMMAND_FILENO);
  fdwalk (close_cb, (void*)PIPE_COMMAND_FILENO);
  
  if (v->lib[0] && setenv("LD_LIBRARY_PATH", v->lib, 1)) {
	res = errno;
	goto exit;
  }

  if (execl(v->bin, v->bin, NULL) == -1) {
	res = errno;
  }

  my_exit(the_obj);
  
 exit:
  if (res)
	dbg("%s\n", strerror(res));

  LEAVE();
  return res;  
}

/* get_voxind checks if the specific voxind (ec, nve) is present and
   update the struct accordingly.
*/
static bool get_voxind(voxind_t *v) {
  ENTER();

  if (!v || !v->rootdir)
	return false;
  
  // if id == ID_NVE: rootdir unchanged and ld_library_path unset
  if (v->id == ID_ECI) {
	// the root directory is RFS32
	// Note: eci.ini is expected to be accessible in the current
	// working directory
	//
	size_t max = sizeof(v->rootdir);
	size_t len = strnlen(v->rootdir, max);
	if (len + strlen(RFS32) >= max)
	  goto exit;
	
	strncpy(v->rootdir+len, RFS32, max-len);
	
	// LD_LIBRARY_PATH
	// e.g if voxinRoot = "/"
	// libraryPath = "/opt/IBM/ibmtts/lib:/opt/oralux/voxin/rfs32/lib:/opt/oralux/voxin/rfs32/usr/lib"
	max = sizeof(v->lib);
	len = snprintf(v->lib,
				   max,
				   "%s/%s/lib:%s" VOXIN_DIR "/rfs32/lib:%s" VOXIN_DIR "/rfs32/usr/lib",
				   v->rootdir, IBMTTS_RO,
				   v->rootdir, v->rootdir);
	if (len >= max) {
	  dbg("path too long\n");
	  goto exit;
	}
  }

  // check presence of binary 
  char buf[MAXBUF];
  size_t len = snprintf(buf, sizeof(buf), "%s/%s/%s", v->rootdir, v->dir, v->bin);
  if (len >= sizeof(buf))
	goto exit;
  
  FILE *fd = fopen(buf, "r");
  if (fd)
	fclose(fd);

  v->present = (fd != NULL);

 exit:
  if (v->present) {
	dbg("%s: dir=%s, bin=%s, rootdir=%s, lib=%s",
		(v->id == ID_ECI) ? "eci" : "nve",
		v->dir, v->bin, v->rootdir, v->lib);
  }
  
  return v->present;
}


static int daemon_start(struct libvoxin_t *the_obj) {
  pid_t child_pid;
  pid_t parent_pid;
  int err = 0;

  ENTER();
  
  if (!the_obj)
	return EINVAL;

#ifdef NO_PIPE
  the_obj->child = 0;  
  return 0;
#endif
  
  parent_pid = getpid();

  err = getVoxinRootDir(voxind[ID_ECI].rootdir, sizeof(voxind[ID_ECI].rootdir));
  if (err) {
	return err;
  }
  strncpy(voxind[ID_NVE].rootdir, voxind[ID_ECI].rootdir, sizeof(voxind[ID_NVE].rootdir));

  int i;
  for (i=0; i<sizeof(voxind); i++) {  
	if (!get_voxind(voxind))
	  continue;
  
	child_pid = fork();
	switch(child_pid) {
	case 0:
	  if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
		exit(errno);
	  }
	  if (getppid() != parent_pid) {
		exit(0);
	  }
	  
	  the_obj->_pipe_close(the_obj->pipe_command, PIPE_SOCKET_PARENT);
	  exit(child(the_obj, voxind));
	  
	  break;
	case -1:
	  err = errno;
	  break;
	default:
	  the_obj->child = child_pid;
	  the_obj->_pipe_close(the_obj->pipe_command, PIPE_SOCKET_CHILD_INDEX);
	  break;
	}
  }

  LEAVE();
  return err;
}


static int daemon_stop(struct libvoxin_t *the_obj)
{  
  ENTER();
  if (!the_obj)
	return EINVAL;

  /* #ifdef NO_PIPE */
  /* #endif */
  if (!the_obj->child) { // TODO
  }
  
  LEAVE();
  return 0;
}


int libvoxin_create(libvoxin_handle_t *i, uint32_t with_eci)
{
  static int once = 0;
  int res = 0;
  struct libvoxin_t *the_obj = NULL;

  ENTER();

  if (!i || once)
	return EINVAL;

  the_obj = (struct libvoxin_t*)calloc(1, sizeof(struct libvoxin_t));
  if (!the_obj)
	return errno;

  the_obj->with_eci = with_eci;
#ifdef NO_PIPE
  the_obj->_pipe_create = no_pipe_create;
  the_obj->_pipe_delete = no_pipe_delete;
  the_obj->_pipe_restore = no_pipe_restore;
  the_obj->_pipe_dup2 = no_pipe_dup2;
  the_obj->_pipe_close = no_pipe_close;
  the_obj->_pipe_read = no_pipe_read;
  the_obj->_pipe_write = no_pipe_write;  
#else
  the_obj->_pipe_create = pipe_create;
  the_obj->_pipe_delete = pipe_delete;
  the_obj->_pipe_restore = pipe_restore;
  the_obj->_pipe_dup2 = pipe_dup2;
  the_obj->_pipe_close = pipe_close;
  the_obj->_pipe_read = pipe_read;
  the_obj->_pipe_write = pipe_write;
#endif
  res = the_obj->_pipe_create(&the_obj->pipe_command);
  
  if (res)    
	goto exit0;

  the_obj->id = LIBVOXIN_ID;
  res = daemon_start(the_obj);
  if (!res) {
	once = 1;
	*i = the_obj;
  }
  
 exit0:
  if (res) {
	daemon_stop(the_obj);
	the_obj->_pipe_delete(&the_obj->pipe_command);
  }

  LEAVE();
  return res;  
}


int libvoxin_delete(libvoxin_handle_t *i)
{
  static int once = 0;
  int res = 0;
  struct libvoxin_t *the_obj = NULL;
  
  ENTER();

  if (!i)
	return EINVAL;

  if (!*i)
	return 0;
  
  the_obj = (struct libvoxin_t*)*i;
  
  if (once)
	return 0;

  res = daemon_stop(the_obj);
  if (!res) {
	res = the_obj->_pipe_delete(&the_obj->pipe_command);
	if (res)    
	  goto exit0;
  }

  if (!res) {
	memset(the_obj, 0, sizeof(*the_obj));
	free(the_obj);
	*i = NULL;
  }

 exit0:
  LEAVE();
  return res;  
}


int libvoxin_call_eci(libvoxin_handle_t i, struct msg_t *msg)
{
  int res;
  struct libvoxin_t *p = (struct libvoxin_t *)i;
  size_t allocated_msg_length;
  size_t effective_msg_length;

  if (!p || !msg || (msg->id != MSG_TO_ECI_ID)) {
	err("LEAVE, args error(%d)",0);
	return EINVAL;
  }

  allocated_msg_length = MSG_HEADER_LENGTH + msg->allocated_data_length;
  effective_msg_length = MSG_HEADER_LENGTH + msg->effective_data_length;

  if ((effective_msg_length > allocated_msg_length) || !msg_string((enum msg_type)msg->func)){
	err("LEAVE, args error(%d)",1);
	return EINVAL;
  }

  msg->count = ++p->msg_count;
  dbg("send msg '%s', length=%d (#%d)",msg_string((enum msg_type)(msg->func)), msg->effective_data_length, msg->count);  
  ssize_t s = effective_msg_length;
  res = p->_pipe_write(p->pipe_command, msg, &s);
  if (res)
	goto exit0;

  effective_msg_length = allocated_msg_length;
  memset(msg, 0, MSG_HEADER_LENGTH);
  s = effective_msg_length;
  res = p->_pipe_read(p->pipe_command, msg, &s);
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


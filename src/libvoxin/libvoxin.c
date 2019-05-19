extern "C" {
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
#include "libvoxin.h"
#include "conf.h"
#include "msg.h"
#include "debug.h"

#define IBMTTS_RO "opt/IBM/ibmtts"
#define IBMTTS_CFG "var/opt/IBM/ibmtts/cfg"
#define VOXIN_DIR "/opt/oralux/voxin"
#define RFS32 VOXIN_DIR "/rfs32"
  // VOXIND: relative path to voxind when current working directory is RFS32
#define VOXIND "usr/bin/voxind"
#define MAXBUF 4096

  struct libvoxin_t {
	uint32_t id;
	uint32_t msg_count;
	pid_t parent;
	pid_t child;
	struct pipe_t *pipe_command;
	uint32_t stop_required;
  };
  
  static void my_exit(struct libvoxin_t *the_obj)
  {
	struct msg_t msg;
	ssize_t l = sizeof(struct msg_t);
	int res;
  
	memset(&msg, 0, MIN_MSG_SIZE);
	msg.id = MSG_EXIT;
  
	dbg("send exit msg");
	res = pipe_write(the_obj->pipe_command, &msg, &l);
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
  static char *getVoxinRootDir()
  {
	char *line = NULL;
	FILE *fd = NULL;
	char *basename = NULL;
	int err = -1;

	fd = fopen("/proc/self/maps", "r");
	if (!fd) {
	  err = errno;
	  dbg("%s\n", strerror(err));
	  goto exit0;
	}

	line = (char *)calloc(1, MAXBUF);
	if (!line) {
	  err = errno;
	  dbg("%s\n", strerror(err));
	  goto exit0;
	}

	while(1) {
	  char *s = fgets(line, MAXBUF, fd);
	  int i;
	  char *libname=NULL;
	  if (!s) {
		dbg("Can't read line\n");
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
		  strcpy(line,"/");
		} else {
		  s[len-ref+1] = 0;
		  basename = strchr(s, '/');
		  if (!basename) {
			dbg("not match (3)\n");
			continue;
		  }
		  bcopy(basename, line, strlen(basename)+1);
		}
		err = 0;
		dbg("line=%s\n", line);
		break;
	  }	  
	}
	
  exit0:
	if (err && line) {
	  free(line);
	  line = NULL;
	}
	if (fd)
	  fclose(fd);
	return line;
  }
  

  static int close_cb(void *data, int fd) {
	int res = 0;
	if (((void*)fd != data) && (close(fd) == -1))
	  res = errno;
	return res;
  }


  static int child(struct libvoxin_t *the_obj)
  {
	int res = 0;
	char *voxinRoot = NULL;
	char *libraryPath = NULL;
	char *buf = NULL;
	int len;
	
	ENTER();

	voxinRoot = getVoxinRootDir();
	if (!voxinRoot) {
	  goto exit;
	}

	libraryPath = (char *)calloc(1, MAXBUF);
	if (!libraryPath) {
	  res = errno;
	  goto exit;
	}

	buf = (char *)calloc(1, MAXBUF);
	if (!buf) {
	  res = errno;
	  goto exit;
	}

	// Change current directory to RFS32
	// Note: eci.ini is expected to be accessible in the current
	// working directory
	// 
	len = snprintf(buf, MAXBUF, "%s/%s", voxinRoot, RFS32);
	if (len >= MAXBUF) {
	  dbg("path too long\n");
	  goto exit;
	}

	if (chdir(buf)) {
	  res = errno;
	  goto exit;
	}

	dbg("cd %s\n", buf);
	/* // voxind path */
	/* len = snprintf(buf, MAXBUF, "%s/%s", voxinRoot, VOXIND); */
	/* if (len >= MAXBUF) { */
	/*   dbg("path too long\n"); */
	/*   goto exit; */
	/* } */

	// LD_LIBRARY_PATH
	len = snprintf(libraryPath,
				   MAXBUF,
				   "%s/%s/lib:%s" VOXIN_DIR "/rfs32/lib:%s" VOXIN_DIR "/rfs32/usr/lib",
				   voxinRoot, IBMTTS_RO,
				   voxinRoot, voxinRoot);
	if (len >= MAXBUF) {
	  dbg("path too long\n");
	  goto exit;
	}
	dbg("LD_LIBRARY_PATH=%s\n", libraryPath);
	pipe_dup2(the_obj->pipe_command, PIPE_SOCKET_CHILD_INDEX, PIPE_COMMAND_FILENO);

	DebugFileFinish();
	fdwalk (close_cb, (void*)PIPE_COMMAND_FILENO);
  
	if (setenv("LD_LIBRARY_PATH", libraryPath, 1)) {
	  res = errno;
	  goto exit;
	}

	// launch voxind
	if (execl(VOXIND, VOXIND, NULL) == -1) {
	  res = errno;
	}

	my_exit(the_obj);
  
  exit:
	if (res)
	  dbg("%s\n", strerror(res));
	if (voxinRoot)
	  free(voxinRoot);
	if (libraryPath)
	  free(libraryPath);
	if (buf)
	  free(buf);
	LEAVE();
	return res;  
  }

  
  static int daemon_start(struct libvoxin_t *the_obj)
  {
	pid_t child_pid;
	pid_t parent_pid;
	int res = 0;

	ENTER();
  
	if (!the_obj)
	  return EINVAL;

	parent_pid = getpid();
	child_pid = fork();
	switch(child_pid) {
	case 0:
	  if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
		exit(errno);
	  }
	  if (getppid() != parent_pid) {
		exit(0);
	  }
	  pipe_close(the_obj->pipe_command, PIPE_SOCKET_PARENT);
	  exit(child(the_obj));
    
	  break;
	case -1:
	  res = errno;
	  break;
	default:
	  the_obj->child = child_pid;
	  pipe_close(the_obj->pipe_command, PIPE_SOCKET_CHILD_INDEX);
	  break;
	}

	LEAVE();
	return res;
  }


  static int daemon_stop(struct libvoxin_t *the_obj)
  {  
	ENTER();
	if (!the_obj)
	  return EINVAL;

	if (!the_obj->child) { // TODO
	}
  
	LEAVE();
	return 0;
  }


  int libvoxin_create(libvoxin_handle_t *i)
  {
	static int once = 0;
	int res = 0;
	struct libvoxin_t *the_obj = NULL;

	ENTER();

	if (!i || once)
	  return EINVAL;

	the_obj = (libvoxin_t*)calloc(1, sizeof(struct libvoxin_t));
	if (!the_obj)
	  return errno;

	res = pipe_create(&the_obj->pipe_command);
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
	  pipe_delete(&the_obj->pipe_command);
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
	  res = pipe_delete(&the_obj->pipe_command);
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

	if ((effective_msg_length > allocated_msg_length) || !msg_string((msg_type)msg->func)){
	  err("LEAVE, args error(%d)",1);
	  return EINVAL;
	}

	msg->count = ++p->msg_count;
	dbg("send msg '%s', length=%d (#%d)",msg_string((msg_type)(msg->func)), msg->effective_data_length, msg->count);  
	ssize_t s = effective_msg_length;
	res = pipe_write(p->pipe_command, msg, &s);
	if (res)
	  goto exit0;

	effective_msg_length = allocated_msg_length;
	memset(msg, 0, MSG_HEADER_LENGTH);
	s = effective_msg_length;
	res = pipe_read(p->pipe_command, msg, &s);
	if (!res && (s >= 0)) {
	  effective_msg_length = (size_t)s;
	  if (!msg_string((msg_type)(msg->func))
		  || (effective_msg_length < MSG_HEADER_LENGTH + msg->effective_data_length)) {
		res = EIO;
	  } else if (msg->id == MSG_EXIT) {
		dbg("recv msg exit");
		// if the child process exits then libvoxin currently exits too.
		sleep(1);
		exit(1);
		//      res = ECHILD;
	  } else {
		dbg("recv msg '%s', length=%d, res=0x%x (#%d)", msg_string((msg_type)(msg->func)), msg->effective_data_length, msg->res, msg->count);
	  }
	}
  
  exit0:
	dbg("LEAVE(res=0x%x)",res);  
	return res;
  }


}

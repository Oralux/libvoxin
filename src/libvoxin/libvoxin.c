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

struct libvoxin_t {
  uint32_t id;
  uint32_t msg_count;
  pid_t parent;
  pid_t child;
  struct pipe_t *pipe_command;
  uint32_t stop_required;
};

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


static int close_cb(void *data, int fd) {
  int res = 0;
  if ((fd != (int)data) && (close(fd) == -1))
    res = errno;
  return res;
}


static int child(struct libvoxin_t *this)
{
  int res = 0;

  ENTER();
  
  pipe_dup2(this->pipe_command, PIPE_SOCKET_CHILD_INDEX, PIPE_COMMAND_FILENO);

  DebugFileFinish();
  fdwalk (close_cb, (void*)PIPE_COMMAND_FILENO);
  
  if (execlp(VOXIND_CMD_ARG0,
	     VOXIND_CMD_ARG0,
	     VOXIND_CMD_ARG1,
	     VOXIND_CMD_ARG2,
	     VOXIND_CMD_ARG3,
	     VOXIND_CMD_ARG4,
	     NULL) == -1) {
    res = errno;
  }

  LEAVE();
  return res;
}

static int daemon_start(struct libvoxin_t *this)
{
  pid_t child_pid;
  pid_t parent_pid;
  int res = 0;

  ENTER();
  
  if (!this)
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
    pipe_close(this->pipe_command, PIPE_SOCKET_PARENT);
    exit(child(this));
    
    break;
  case -1:
    res = errno;
    break;
  default:
    this->child = child_pid;
    pipe_close(this->pipe_command, PIPE_SOCKET_CHILD_INDEX);
    break;
  }

  LEAVE();
  return res;
}


static int daemon_stop(struct libvoxin_t *this)
{  
  ENTER();
  if (!this)
    return EINVAL;

  if (!this->child) { // TODO
  }
  
  LEAVE();
  return 0;
}


int libvoxin_create(libvoxin_handle_t *i)
{
  static int once = 0;
  int res = 0;
  struct libvoxin_t *this = NULL;

  ENTER();

  if (!i || once)
    return EINVAL;

  this = calloc(1, sizeof(struct libvoxin_t));
  if (!this)
    return errno;

  res = pipe_create(&this->pipe_command);
  if (res)    
    goto exit0;

  this->id = LIBVOXIN_ID;
  res = daemon_start(this);
  if (!res) {
	once = 1;
	*i = this;
  }
  
 exit0:
  if (res) {
	daemon_stop(this);
    pipe_delete(&this->pipe_command);
  }

  LEAVE();
  return res;  
}


int libvoxin_delete(libvoxin_handle_t *i)
{
  static int once = 0;
  int res = 0;
  struct libvoxin_t *this = NULL;
  
  ENTER();

  if (!i)
    return EINVAL;

  if (!*i)
    return 0;
  
  this = (struct libvoxin_t*)*i;
  
  if (once)
    return 0;

  res = daemon_stop(this);
  if (!res) {
    res = pipe_delete(&this->pipe_command);
    if (res)    
      goto exit0;
  }

  if (!res) {
    memset(this, 0, sizeof(*this));
    free(this);
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

  if ((effective_msg_length > allocated_msg_length) || !msg_string(msg->func)){
    err("LEAVE, args error(%d)",1);
    return EINVAL;
  }

  msg->count = ++p->msg_count;
  dbg("send msg '%s', length=%d (#%d)",msg_string(msg->func), msg->effective_data_length, msg->count);
  res = pipe_write(p->pipe_command, msg, &effective_msg_length);
  if (res)
    goto exit0;

  effective_msg_length = allocated_msg_length;
  memset(msg, 0, MSG_HEADER_LENGTH);
  res = pipe_read(p->pipe_command, msg, &effective_msg_length);
  if (!res) {
    if (!msg_string(msg->func)
	|| (effective_msg_length < MSG_HEADER_LENGTH + msg->effective_data_length)) {
      res = EIO;
    } else {
      dbg("recv msg '%s', length=%d, res=0x%x (#%d)", msg_string(msg->func), msg->effective_data_length, msg->res, msg->count);
    }
  }
  
 exit0:
  dbg("LEAVE(res=0x%x)",res);  
  return res;
}



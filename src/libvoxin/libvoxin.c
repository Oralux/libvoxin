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


static int close_cb(void *data, int fd) {
  int res = 0;
  if (((void*)fd != data) && (close(fd) == -1))
	res = errno;
  return res;
}


static int child(struct libvoxin_t *the_obj)
{
  int res = 0;

  ENTER();
  
  pipe_dup2(the_obj->pipe_command, PIPE_SOCKET_CHILD_INDEX, PIPE_COMMAND_FILENO);

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

  my_exit(the_obj);
  
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

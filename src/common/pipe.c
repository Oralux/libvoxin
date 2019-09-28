#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "pipe.h"
#include "debug.h"

static int pipe_alloc(struct pipe_t **px)
{
  int res = 0;
  struct pipe_t *p = NULL;
  
  ENTER();

  if (!px)
    return EINVAL;

  p = calloc(1, sizeof(struct pipe_t));
  if (!p)
    return errno;
  
  *px = p;

 exit0:

  LEAVE();
  return res;
}


static int pipe_free(struct pipe_t **px)
{
  struct pipe_t *p;

  ENTER();

  if (!px)
    return EINVAL;
  
  p = *px;
  if (!p)
    return 0;
  
  free(p);
  *px = NULL;

  LEAVE();
  return 0;
}


int pipe_create(struct pipe_t **px)
{
  int res = 0;
  
  dbg("[pid=%lu] ENTER", libvoxinDebugGetTid());

  if (!px)
    return EINVAL;

  res = pipe_alloc(px);
  if (res)
    goto exit0;
  
  res = socketpair(AF_UNIX, SOCK_DGRAM, 0, (*px)->sv);
  if (res == -1) {
    res = errno;
    goto exit0;
  }

  (*px)->ind = PIPE_SOCKET_PARENT;

 exit0:
  if (res) {
    pipe_free(px);
    *px = NULL;
	dbg("[pid=%lu] LEAVE", libvoxinDebugGetTid());
  } else {
	dbg("[pid=%lu] LEAVE (0:%d, 1:%d) ", libvoxinDebugGetTid(), (*px)->sv[0], (*px)->sv[1]);
  }

  return res;
}


int pipe_restore(struct pipe_t **px, int fd)
{
  int res = 0;
  
  ENTER();

  if (!px)
    return EINVAL;

  res = pipe_alloc(px);
  if (res)
    goto exit0;

  (*px)->sv[PIPE_SOCKET_CHILD_INDEX] = fd;
  (*px)->ind = PIPE_SOCKET_CHILD_INDEX;

 exit0:
  if (res) {
    pipe_free(px);
	dbg("[pid=%lu] LEAVE", libvoxinDebugGetTid());	
  } else {
	dbg("[pid=%lu] LEAVE (0:%d, 1:%d)", libvoxinDebugGetTid(), (*px)->sv[0], (*px)->sv[1]);	
  }

  return res;
}


int pipe_delete(struct pipe_t **px)
{
  ENTER();
  return pipe_free(px);
}

static int poll_in(int fd) {
  int err = 0;  // 0 = OK
  int res = 0;
  struct pollfd pfd;
  
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  res = poll(&pfd, 1, 300);
  err = errno;
  if ((res > 0) && (pfd.revents & POLLIN)) {
	err = 0;
  } else if (res == -1) {
	dbg("poll: ko (%s)", strerror(err));
  } else if (res == 0) {
	dbg("poll: ko (timeout)");
	err = EBADFD;
  } else {
	dbg("poll: ko");
	err = EBADFD;
  }
  return err;
}

int pipe_read(struct pipe_t *p, void *buf, ssize_t *len)
{
  int res = 0;
  
  ENTER();
  
  if (!p || !buf || !len) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }

  while(1) {
	res = poll_in(p->sv[p->ind]);
	if (res) {
      err("KO (%d)", res);	  
	  goto exit0;
	}	  
	*len = read(p->sv[p->ind], buf, *len);
    if (*len == 0) {
      err("0 bytes!");
      break;
    } else if (*len > 0) {
      msg("OK (%d bytes)", (int)*len);    
      break;
    } else if ((*len == -1) && (errno != EINTR)) {
      res = errno;
      err("KO (%d)", errno);
      break;
    }
  }
  
 exit0:
  LEAVE();
  return res;
}

int pipe_write(struct pipe_t *p, void *buf, ssize_t *len)
{
  int res = 0;
  
  ENTER();
  
  if (!p || !buf || !len) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }

  while(1) {
    *len = write(p->sv[p->ind], buf, *len);
    if (*len == 0) {
      err("0 bytes!");
      break;
    } else if (*len > 0) {
      msg("OK (%d bytes)", (int)*len);    
      break;
    } else if ((*len == -1) && (errno != EINTR)) {
      res = errno;
      err("KO (%d)", errno);
      break;
    }
  }
  
  LEAVE();
  return res;
}


int pipe_dup2(struct pipe_t *p, int index, int new_fd)
{
  int res = 0;

  // No call to any (debug) function opening descriptors (e.g. descr 3
  // is going to be used)
  
  if (!p) {
    return EINVAL;
  }

  do {
    res = dup2(p->sv[index], new_fd);
    if (res == -1) {
      if (errno == EINTR)
	continue;
      res = errno;
      goto exit0;
    }
    break;
  } while(1);

 exit0:
  if (res) {
	dbg("[pid=%lu] LEAVE", libvoxinDebugGetTid());	
  } else {
	dbg("[pid=%lu] LEAVE (0:%d, 1:%d)", libvoxinDebugGetTid(), p->sv[0], p->sv[1]);	
  }

  return res;
}


int pipe_close(struct pipe_t *p, int index)
{
  dbg("[pid=%lu] ENTER", libvoxinDebugGetTid());
  
  if (!p) {
	dbg("[pid=%lu] LEAVE", libvoxinDebugGetTid());	
    return EINVAL;
  } else {
	dbg("[pid=%lu] LEAVE (%d:%d)", libvoxinDebugGetTid(), index, p->sv[index]);	
  }
  
  return close(p->sv[index]);
}

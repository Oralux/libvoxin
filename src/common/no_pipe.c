//#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
//#include <string.h>
//#include <sys/types.h>
#include <dlfcn.h>
#include "no_pipe.h"
//#include "conf.h"
#include "debug.h"

extern int voxind_read(void *buf, ssize_t *len);
extern int voxind_write(void *buf, ssize_t *len);

typedef int (*t_voxind_read)(void *buf, ssize_t *len);
typedef int (*t_voxind_write)(void *buf, ssize_t *len);


typedef struct {
  t_voxind_read _voxind_read;
  t_voxind_write _voxind_write;
  void *libHandle;
} priv_t;

#define LIBVOXIND "libvoxind.so"

int no_pipe_create(struct pipe_t **px)
{
  struct pipe_t *p = NULL;
  priv_t *priv = NULL;
  
  ENTER();

  if (!px)
    return EINVAL;

  p = *px;

  p = (struct pipe_t *)calloc(1, sizeof(struct pipe_t));
  if (!p)
	return ENOMEM;

  p->priv = calloc(1, sizeof(priv_t));  
  if (!p->priv) {
	goto exit0;
  }

  priv = p->priv;
  priv->libHandle = (void *)dlopen(LIBVOXIND, RTLD_NOW);
  if (!priv->libHandle) {
	err("Can't load %s (%s)\n", LIBVOXIND, dlerror());
	goto exit0;
  }

  priv->_voxind_read = (t_voxind_read)dlsym(priv->libHandle, "voxind_read");
  priv->_voxind_write = (t_voxind_write)dlsym(priv->libHandle, "voxind_write");
  
  return 0;

 exit0:
  if (p) {
	if (priv) {
	  if (priv->libHandle) {
		dlclose(priv->libHandle);
		priv->libHandle = NULL;
	  }
	  free(priv);
	  p->priv = NULL;
	}
	free(p);
	*px = NULL;
  }
  return ENOMEM;	
}


int no_pipe_restore(struct pipe_t **px, int fd)
{
  ENTER();

  if (!px)
    return EINVAL;

  return 0;  
}


int no_pipe_delete(struct pipe_t **px)
{
  struct pipe_t *p = NULL;

  if (!px)
    return EINVAL;

  if (!*px)
    return 0;

  p = *px;
  
  if (p->priv) {
	priv_t *priv = p->priv;
	if (priv->libHandle)
	  dlclose(priv->libHandle);
	free(priv);
	p->priv = NULL;
  }

  free(p);
  *px = NULL;  
  return 0;
}


int no_pipe_read(struct pipe_t *p, void *buf, ssize_t *len)
{
  int res = 0;
  priv_t *priv = NULL;
  
  ENTER();
  
  if (!p || p->priv || !buf || !len) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }

  priv = p->priv;
  if (!priv->_voxind_read) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }
  
  res = priv->_voxind_read(buf, len);
  if (*len == 0) {
	err("0 bytes!");
  } else if (*len > 0) {
	msg("OK (%d bytes)", (int)*len);    
  } else if (*len == -1) {
	res = errno;
  }
  
  LEAVE();
 return res;
}

int no_pipe_write(struct pipe_t *p, void *buf, ssize_t *len)
{
  int res = 0;
  priv_t *priv = NULL;
  
  ENTER();
  
  if (!p || p->priv || !buf || !len) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }

  priv = p->priv;
  if (!priv->_voxind_write) {
    err("KO (%d)", EINVAL);
    return EINVAL;
  }

  res = priv->_voxind_write(buf, len);
  
  LEAVE();
  return res;
}


int no_pipe_dup2(struct pipe_t *p, int index, int new_fd)
{
  return 0;
}


int no_pipe_close(struct pipe_t *p, int index)
{
  ENTER();  
  return 0;
}

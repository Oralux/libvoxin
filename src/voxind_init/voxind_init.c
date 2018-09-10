#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "conf.h"
#include "pipe.h"
#include "debug.h"

#define RFS32 "../rfs32"
#define DAEMON_UID 1
#define DAEMON_GID 1

static char *getSelfPath()
{
  char *resolved_path = realpath("/proc/self/exe", NULL);
  
  if (!resolved_path) {
	dbg("err=%s\n",strerror(errno));
  } else {  
	char *p = strrchr(resolved_path, '/');
	if (p) {
	    *p=0;
	} else {
	  free(resolved_path);
	  resolved_path = NULL;	
	}
  }

  return resolved_path;
}

static int close_cb(int fd0, int fd) {
  int res = 0;
  if ((fd != fd0) && (close(fd) == -1))
  	res = errno;
  return res;
}

// fdwalk function from glib (GPLv2):
// https://git.gnome.org/browse/glib/tree/glib/gspawn.c?h=2.50.0#n1027
static int fdwalk (int (*cb)(int fdx, int fd), int fd0)
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

		  if ((res = cb (fd0, fd)) != 0)
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
	if ((res = cb (fd0, fd)) != 0)
	  break;

  return res;
}

void main(int argc, char** argv)
{
  int res, l1, l2;
  char *rfs32;
  char *resolved_path = getSelfPath();

  if (!resolved_path) {
	res = errno;
	dbg("self path not found!");
	exit(res);
  }

  l1 = strlen(resolved_path);
  l2 = strlen(RFS32);	
  rfs32 = realloc(resolved_path, l1 + l2 + 10);  
  if (!rfs32) {
	res = errno;
	free(resolved_path);
	dbg("realloc fails\n");
	exit(res);	
  }
  
  sprintf(rfs32 + l1, "/%s", RFS32);

  if (chdir(rfs32)) {
	res = errno;
	dbg("chdir(%s): err=%s", RFS32, strerror(res));
	exit(res);
  }
  if (chroot(".")) {
	res = errno;
	dbg("chroot: err=%s",strerror(res));
	exit(res);
  }
   
  fdwalk (close_cb, PIPE_COMMAND_FILENO);

  if (setgid(DAEMON_UID)) {
	res = errno;
	dbg("setgid, err=%s", strerror(res));
	exit(1);
  }
  if (setuid(DAEMON_GID)) {
	res = errno;
	dbg("setuid, err=%s", strerror(res));
	exit(1);
  }

  msg("Lauching voxind...");

  if (execlp(VOXIND, VOXIND, NULL) == -1) {
	res = errno;
	dbg("execlp, err=%s",strerror(res));
  }
}


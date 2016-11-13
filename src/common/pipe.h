#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_SOCKET_PARENT_INDEX 0
#define PIPE_SOCKET_CHILD_INDEX 1

// max size of transfered block between client and server
#define PIPE_MAX_BLOCK (100*1024)

#define PIPE_COMMAND_FILENO 3

struct pipe_t {
  int sv[2]; /* socketpair descriptors (0=PARENT, 1=CHILD)*/
  int ind; /* sv[ind] current valid descriptor */
};

extern int pipe_create(struct pipe_t **px);
extern int pipe_delete(struct pipe_t **px);
extern int pipe_restore(struct pipe_t **px, int fd);
extern int pipe_dup2(struct pipe_t *p, int index, int new_fd);
extern int pipe_close(struct pipe_t *p, int index);
extern int pipe_read(struct pipe_t *p, void *buf, ssize_t *len);
extern int pipe_write(struct pipe_t *p, void *buf, ssize_t *len);

#endif

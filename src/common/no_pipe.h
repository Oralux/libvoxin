#ifndef NO_PIPE_H
#define NO_PIPE_H

#include <stdint.h>
#include "pipe.h"

extern int no_pipe_create(struct pipe_t **px);
extern int no_pipe_restore(struct pipe_t **px, int fd);
extern int no_pipe_delete(struct pipe_t **px);
extern int no_pipe_read(struct pipe_t *p, void *buf, ssize_t *len);
extern int no_pipe_write(struct pipe_t *p, void *buf, ssize_t *len);
extern int no_pipe_dup2(struct pipe_t *p, int index, int new_fd);
extern int no_pipe_close(struct pipe_t *p, int index);

#endif

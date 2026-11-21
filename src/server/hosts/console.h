#ifndef MH_SERVER_HOSTS_CONSOLE_H
#define MH_SERVER_HOSTS_CONSOLE_H

#include "engine/core/error.h"

/*
 * One process console owner, acquired before serving and released after close.
 */
enum mh_error_code	mh_server_console_acquire(struct mh_error *error_out);
void			mh_server_console_release(void);
int			mh_server_console_stopped(void);

/* Nonblocking input: zero bytes means pending unless eof is set. */
enum mh_error_code	mh_server_console_read(char *bytes, size_t capacity,
	size_t *size, int *eof, struct mh_error *error_out);

#endif

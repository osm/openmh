#define _POSIX_C_SOURCE 200112L
#include <sys/select.h>

#include <unistd.h>

#include "stdin_ready.h"

int
mh_test_stdin_ready(void)
{
	struct timeval timeout;

	fd_set descriptors;

	FD_ZERO(&descriptors);
	FD_SET(STDIN_FILENO, &descriptors);
	timeout.tv_sec = timeout.tv_usec = 0;

	return select(STDIN_FILENO + 1, &descriptors, NULL, NULL, &timeout);
}

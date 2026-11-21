#define _POSIX_C_SOURCE 200112L
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "server/hosts/console.h"

static volatile sig_atomic_t stopped;
static struct sigaction previous_interrupt, previous_terminate;

static void
stop_signal(int number)
{
	(void)number;
	stopped = 1;
}

enum mh_error_code
mh_server_console_acquire(struct mh_error *error_out)
{
	struct sigaction action;

	stopped = 0;
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_signal;
	sigemptyset(&action.sa_mask);

	if (sigaction(SIGINT, &action, &previous_interrupt) != 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot install server shutdown signals");

	if (sigaction(SIGTERM, &action, &previous_terminate) != 0) {
		sigaction(SIGINT, &previous_interrupt, NULL);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot install server shutdown signals");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_server_console_release(void)
{
	sigaction(SIGTERM, &previous_terminate, NULL);
	sigaction(SIGINT, &previous_interrupt, NULL);
}

int
mh_server_console_stopped(void)
{
	return stopped != 0;
}

enum mh_error_code
mh_server_console_read(char *bytes, size_t capacity, size_t *size, int *eof,
	struct mh_error *error_out)
{
	struct pollfd input;
	ssize_t count;

	*size = 0;
	*eof = 0;
	input.fd = STDIN_FILENO;
	input.events = POLLIN;
	input.revents = 0;

	if (poll(&input, 1, 0) <= 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	count = read(STDIN_FILENO, bytes, capacity);

	if (count <= 0)
		*eof = 1;
	else
		*size = (size_t)count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

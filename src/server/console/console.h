#ifndef MH_SERVER_CONSOLE_H
#define MH_SERVER_CONSOLE_H

#include <stddef.h>

#include "engine/core/error.h"
#include "server/logging/events.h"

struct mh_server_host;

enum {
	MH_SERVER_CONSOLE_LINE_BYTES = 256
};

struct mh_server_console {
	char	line[MH_SERVER_CONSOLE_LINE_BYTES];
	size_t	used;
	int	enabled, discard;
};

void	mh_server_console_init(struct mh_server_console *console, int enabled);

/* Output is independent of stdin EOF; DisableIO is selected by the CLI. */
void	mh_server_console_logger(struct mh_server_event_logger *logger);

/* Native stdin polling. EOF disables input; it does not shut down the host. */
enum mh_error_code	mh_server_console_poll(
	struct mh_server_console *console, struct mh_server_host *host,
	int *quit, struct mh_error *error_out);

#endif

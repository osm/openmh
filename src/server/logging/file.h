#ifndef MH_SERVER_LOGGING_FILE_H
#define MH_SERVER_LOGGING_FILE_H

#include "server/logging/race.h"

/*
 * Native server log sink. Borrows the configured path through host shutdown.
 * Append failures are reported without stopping a race, as in the original.
 * DisableIO suppresses the diagnostic, not the configured race file.
 */
struct mh_server_log_file {
	const char	*path;
	int		quiet;
};

void	mh_server_log_file_init(struct mh_server_log_file *file,
	const char *path, int quiet, struct mh_server_race_logger *logger);

#endif

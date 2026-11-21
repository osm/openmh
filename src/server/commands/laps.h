#ifndef MH_SERVER_LAPS_H
#define MH_SERVER_LAPS_H

#include "server/commands/client.h"

/* Caller has authenticated a lobby player and excluded an active race. */
enum mh_error_code	mh_server_lap_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out);

#endif

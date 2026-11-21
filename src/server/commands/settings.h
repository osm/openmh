#ifndef MH_SERVER_SETTINGS_H
#define MH_SERVER_SETTINGS_H

#include "server/commands/client.h"

/* Authenticated lobby commands, after the active-race permission check. */
enum mh_error_code	mh_server_track_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out);
enum mh_error_code	mh_server_mode_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	struct mh_server_command_output *output, struct mh_error *error_out);
enum mh_error_code	mh_server_grid_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	struct mh_server_command_output *output, struct mh_error *error_out);

#endif

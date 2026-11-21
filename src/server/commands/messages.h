#ifndef MH_SERVER_MESSAGES_H
#define MH_SERVER_MESSAGES_H

#include "server/commands/client.h"

/* Private command helper; caller has authenticated a lobby sender. */
enum mh_error_code	mh_server_message_command(
	const struct mh_server_lobby *lobby, unsigned int player,
	const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out);

#endif

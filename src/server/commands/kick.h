#ifndef MH_SERVER_KICK_COMMAND_H
#define MH_SERVER_KICK_COMMAND_H

#include "server/commands/client.h"

void	mh_server_kick_command(struct mh_server_lobby *lobby,
	unsigned int player, const char *arguments,
	struct mh_server_command_output *output);

#endif

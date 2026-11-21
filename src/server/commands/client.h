#ifndef MH_SERVER_CLIENT_COMMAND_H
#define MH_SERVER_CLIENT_COMMAND_H

#include "server/commands/information.h"
#include "server/session/ready.h"

enum {
	MH_SERVER_COMMAND_BYTES = 256,
	MH_SERVER_COMMAND_REPLIES = MH_SERVER_TEXT_LINES
};

struct mh_server_command_reply {
	/* Explicit console audience, independent of network recipients. */
	int		log;
	unsigned int	recipients;
	unsigned long	color;
	char		text[MH_SERVER_COMMAND_BYTES];
};

struct mh_server_command_output {
	int				forward;
	char				chat[MH_NET_CHAT_BYTES];
	int				refresh_roster;
	unsigned int			count;
	struct mh_server_command_reply	replies[MH_SERVER_COMMAND_REPLIES];
};

/*
 * Consume a chat packet from its owning peer. Replies use server slot zero
 * and reliable chat. Ordinary chat goes to other players in the sender's
 * lobby/race audience. Racing senders bypass lobby command parsing.
 * race_active also restricts commands from players waiting in the lobby.
 */
enum mh_error_code	mh_server_client_command(struct mh_server_lobby *lobby,
	struct mh_server_ready *ready, const struct mh_server_config *config,
	int race_active, unsigned long peer,
	const struct mh_net_message *message,
	struct mh_server_command_output *output, struct mh_error *error_out);

#endif

#ifndef MH_SERVER_OPERATOR_H
#define MH_SERVER_OPERATOR_H

#include "server/commands/information.h"

enum mh_server_operator_action {
	MH_SERVER_OPERATOR_NONE,
	MH_SERVER_OPERATOR_LOBBY,
	MH_SERVER_OPERATOR_QUIT,
	MH_SERVER_OPERATOR_CLEAR,
	MH_SERVER_OPERATOR_START,
	MH_SERVER_OPERATOR_USER,
	MH_SERVER_OPERATOR_KICK
};

struct mh_server_operator_output {
	enum mh_server_operator_action	action;
	int				refresh_roster;
	int				refresh_identity;
	int				chat;
	unsigned int			player;
	struct mh_server_text		text;
};

/* Portable console semantics; host owns delivery and lifecycle actions. */
enum mh_error_code	mh_server_operator_command(
	struct mh_server_lobby *lobby, const struct mh_server_config *config,
	int race_active, const char *line,
	struct mh_server_operator_output *output, struct mh_error *error_out);

#endif

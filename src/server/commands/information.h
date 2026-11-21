#ifndef MH_SERVER_INFORMATION_H
#define MH_SERVER_INFORMATION_H

#include "interfaces/network/network.h"
#include "server/session/lobby.h"

enum {
	MH_SERVER_TEXT_BYTES = 256,
	/*
	 * Full /user output, including both membership flags and blank lines.
	 */
	MH_SERVER_TEXT_LINES = 21
};

struct mh_server_text {
	unsigned int	count;
	char		lines[MH_SERVER_TEXT_LINES][MH_SERVER_TEXT_BYTES];
};

/* Caller supplies validated server state; each operation replaces the text. */
void	mh_server_information_help(int operator_console,
	struct mh_server_text *text);
void	mh_server_information_tracks(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, struct mh_server_text *text);
void	mh_server_information_welcome(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, struct mh_server_text *text);
void	mh_server_information_status(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, int operator_console,
	struct mh_server_text *text);
void	mh_server_information_users(const struct mh_server_lobby *lobby,
	struct mh_server_text *text);

void	mh_server_information_user(const struct mh_server_lobby *lobby,
	unsigned int player, const struct mh_transport_address *address,
	struct mh_server_text *text);

#endif

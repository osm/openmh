#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_LOBBY_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_LOBBY_H

#include "client/session/lobby.h"

/* Native services and resources; ownership follows the portable constructor. */
enum mh_error_code	mh_network_lobby_open(const char *asset_root, int host,
	struct mh_game_timer *timer, const struct mh_debug_options *debug,
	struct mh_network_lobby **output, struct mh_error *error_out);

#endif

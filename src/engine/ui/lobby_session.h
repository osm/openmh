#ifndef MH_UI_LOBBY_SESSION_H
#define MH_UI_LOBBY_SESSION_H

#include "engine/core/error.h"
#include "engine/network/session.h"
#include "engine/ui/lobby_roster.h"

/*
 * Live admitted-session feed for the settings/player compositor. The session
 * name belongs to discovery/transport, not an application packet. Local
 * unadvertised profile edits survive older received rosters. Membership comes
 * from reliable session state; type-15 status supplies position/lap/ping. No
 * SDL, rendering, network I/O or session mutation; atomic on error.
 */
enum mh_error_code	mh_lobby_session_roster(
	const struct mh_net_session *session, const char *session_name,
	unsigned int password_mode, struct mh_lobby_roster *output,
	struct mh_error *error_out);

#endif

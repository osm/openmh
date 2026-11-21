#ifndef MH_CLIENT_SESSION_NETWORK_GAME_H
#define MH_CLIENT_SESSION_NETWORK_GAME_H

#include "client/application/activity.h"
#include "client/session/options.h"
#include "engine/audio/menu_playback.h"
#include "engine/core/error.h"
#include "engine/game/standings.h"
#include "engine/network/network_session.h"
#include "engine/ui/lobby_settings.h"

/*
 * Optional external frontend, zeroed before launch and borrowed until close.
 * The session publishes network while open; callers may inspect it and queue
 * lobby commands on the same thread, but must not poll or close it. Close
 * clears that pointer. leave ends the connection; return_to_lobby retires only
 * the current race. Results remain copied here after race assets are released.
 */
struct mh_network_game_control {
	struct mh_network	*network;
	int			leave, return_to_lobby, racing, finished;
	unsigned long		results_revision;
	struct mh_standings	results;
};

struct mh_network_game_request {
	struct mh_network_game_control		*control;
	int					host;

	/* Provider-owned targets borrowed through activity close/reconnect. */
	const struct mh_transport_target	*local, *remote;
	const char				*name;

	/* Menus allow this prefix of the sorted unlocked courses. */
	unsigned int				track_limit;
	struct mh_net_session_config		config;
};

/*
 * Main-thread graphical session: admission/password retry, lobby, repeated
 * races and reconnect. All arguments are borrowed except *menu_audio: its
 * effects stop on entry, music continues in the lobby and the object is
 * closed/cleared before loading the first race. It is also closed when the
 * activity finishes. Settings retain race AV edits and lobby return fields;
 * particle_random retains the particle stream and any original horn lookup
 * reseeding. Window close sets quit_requested; ordinary lobby departure returns
 * to the caller.
 *
 * Resumable session, borrowing every argument until activity close. Output
 * must be initialized and empty. Normal exit polls the native 250 ms leave
 * grace period without blocking; cancellation releases resources immediately.
 * Preparation failures can still start an activity: it retains the diagnostic
 * and reports failure after its leave phase. Argument/allocation failures
 * return directly without starting the output.
 */
enum mh_error_code	mh_network_game_open(const char *asset_root,
	const struct mh_network_game_request *request,
	const struct mh_session_options *options,
	const struct mh_lobby_settings_catalog *catalog,
	struct mh_settings *settings, unsigned long *particle_random,
	struct mh_menu_audio **menu_audio, int *quit_requested,
	struct mh_activity *output, struct mh_error *error_out);

#endif

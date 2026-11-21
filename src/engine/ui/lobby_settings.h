#ifndef MH_UI_LOBBY_SETTINGS_H
#define MH_UI_LOBBY_SETTINGS_H

#include "engine/core/error.h"
#include "engine/network/session.h"
#include "engine/settings/config.h"
#include "interfaces/audio/horn.h"

struct mh_lobby_settings_catalog {
	/* Settings source-index order. */
	struct mh_settings_names	cars, tracks;
	const struct mh_horn_catalog	*horns;
};

/*
 * Retain only local profile preferences, without server course/lap choices.
 * Same ownership, validation and atomicity rules as the full return below.
 */
enum mh_error_code	mh_lobby_settings_profile(
	const struct mh_net_session *session,
	const struct mh_lobby_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, struct mh_error *error_out);

/*
 * Lobby teardown and mode-4 frontend restore. Use the locally edited profile
 * and host setup (received roster for clients), even after leave/disconnection.
 * A client without an assigned slot changes nothing. Unknown car/track names
 * retain their previous selection; unknown horns use the seeded fallback.
 * Single-player recording, password, mode, catch-up and unrelated settings
 * remain the saved frontend choices. Call before disposing of the session.
 * Errors preserve settings/random.
 */
enum mh_error_code	mh_lobby_settings_return(
	const struct mh_net_session *session,
	const struct mh_lobby_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, struct mh_error *error_out);

#endif

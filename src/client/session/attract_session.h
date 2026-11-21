#ifndef MH_CLIENT_SESSION_ATTRACT_SESSION_H
#define MH_CLIENT_SESSION_ATTRACT_SESSION_H

#include "client/application/activity.h"
#include "client/session/options.h"
#include "engine/core/error.h"

struct mh_horn_catalog;
struct mh_profile_catalog;
struct mh_settings;

/*
 * Automatic eight-AI race. Preset and mode overrides use a runtime copy;
 * only the five original returning pause options are copied back. Does
 * not open records, rankings, recordings or League storage.
 * previous_track retains the last course across successful loads. Window close
 * sets quit_requested; raw Enter/Escape sets manual_exit, for the menu idle
 * delay.
 *
 * Initialize an empty output activity. Borrowed inputs and result slots stay
 * live until close; cancellation releases resources without finishing a race.
 */
enum mh_error_code	mh_attract_session_open(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *saved_settings,
	const struct mh_session_options *options, char previous_track[16],
	unsigned long *random, int *manual_exit, int *quit_requested,
	struct mh_activity *output, struct mh_error *error_out);

#endif

#ifndef MH_CLIENT_SESSION_SINGLE_SESSION_H
#define MH_CLIENT_SESSION_SINGLE_SESSION_H

#include "client/application/activity.h"
#include "client/session/options.h"
#include "client/session/single_setup.h"
#include "engine/core/error.h"

struct mh_demo_file;

/* Host commands/results, separate from physics input. Zero before launch. */
struct mh_single_control {
	int			leave, finished;
	struct mh_standings	results;
};

/*
 * Resumable session. Initialize output before use; it must not already own an
 * activity. Borrowed settings, asset root, controls, music path, random and
 * result slots remain live until close. Successful completion seals a take;
 * cancellation only releases resources.
 */
enum mh_error_code	mh_single_session_open(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out);

/*
 * Interactive Quick Race/Time Attack, including records and optional MDE
 * recording. Return MH_ERROR_NONE on success; quit_requested reports a
 * window-close request. Preferences retain race-time AV edits; random retains
 * the session stream. The caller saves preferences at its own launch/return
 * boundary. A normal return can hand off a sealed private take, owned by the
 * caller for the save/name dialog. Short takes, errors and window close leave
 * *take NULL.
 */
enum mh_error_code	mh_single_session_run(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, struct mh_demo_file **take,
	struct mh_error *error_out);
#endif

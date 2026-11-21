#ifndef MH_CLIENT_SESSION_DEMO_SESSION_H
#define MH_CLIENT_SESSION_DEMO_SESSION_H

#include "client/application/activity.h"
#include "client/session/options.h"
#include "engine/core/error.h"

enum mh_demo_session_mode {
	MH_DEMO_SESSION_WATCH,
	MH_DEMO_SESSION_GHOST,
	MH_DEMO_SESSION_BENCHMARK
};

struct mh_demo_file;
struct mh_file;
struct mh_horn_catalog;
struct mh_settings;

/*
 * Interactive Watch or Benchmark (mode 6), or live Ghost race (mode 3). Borrows
 * preferences/horn catalog; owns the selected file for the session. Ghost
 * supports personal records and optional recording. Demo watching neither
 * records itself nor updates record books. Benchmark uses the track camera,
 * returns at EOF and saves Fps.txt through options->user_data. Cancellation
 * leaves the report untouched. Ordinary Watch EOF retains the last pose;
 * pause-menu Quit returns to the menus, while window close sets quit_requested.
 * The caller saves preferences at the boundaries. A normal Ghost return hands
 * off a closed private take for the save dialog; short takes, errors, Demo
 * watching and window close leave *take NULL.
 *
 * Initialize an empty output activity. Borrowed inputs and result slots stay
 * live until close; cancellation releases resources without finishing a race.
 * demo_path is read through options->resources, never a native fallback.
 */
enum mh_error_code	mh_demo_session_open(const char *asset_root,
	const char *demo_path, enum mh_demo_session_mode mode,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out);

/*
 * Playback from an already loaded recording, including a user-data view.
 * Success adopts the malloc-owned bytes and clears the supplied file; failure
 * leaves it unchanged and caller-owned. The session retains its allocation
 * through close because decoded frames borrow the original byte buffer.
 * recording_name is borrowed through close and required for Benchmark reports.
 */
enum mh_error_code	mh_demo_session_open_bytes(const char *asset_root,
	struct mh_file *bytes, const char *recording_name,
	enum mh_demo_session_mode mode, const struct mh_horn_catalog *horns,
	struct mh_settings *settings, const struct mh_session_options *options,
	unsigned long *random, int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out);

#endif

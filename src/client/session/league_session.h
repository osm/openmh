#ifndef MH_CLIENT_SESSION_LEAGUE_SESSION_H
#define MH_CLIENT_SESSION_LEAGUE_SESSION_H

#include "client/application/activity.h"
#include "client/session/leagues.h"
#include "client/session/options.h"
#include "engine/core/error.h"

struct mh_demo_file;
struct mh_horn_catalog;
struct mh_settings;

/*
 * Play the selected League race, accepting results into its saved document.
 * A completed season presents its board/video and then rolls over; an already
 * completed save resumes that boundary without racing/awarding again.
 * Championship completion removes only the winning save. creation is used
 * for the replacement Default if it was the last league. Caller owns a
 * returned recording take and saves preferences. Window close sets
 * quit_requested; no rollover/removal follows a window close or
 * presentation/save failure. progress_directory selects the external FLG
 * directory; the original unlock_cheat makes tracks available in memory and
 * suppresses marker writes. The caller's menu resources survive the entire
 * handoff.
 *
 * Resumable League race/season/video operation. Same output and cancellation
 * contract as mh_single_session_open. The creation descriptor is copied;
 * its profile/player/car pointers and the other borrowed inputs live until
 * close.
 */
enum mh_error_code	mh_league_session_open(const char *asset_root,
	const char *disc_root, const char *progress_directory, int unlock_cheat,
	struct mh_leagues *leagues, const struct mh_league_creation *creation,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, int *champion, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out);

#endif

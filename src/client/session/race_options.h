#ifndef MH_SESSION_RACE_OPTIONS_H
#define MH_SESSION_RACE_OPTIONS_H
#include "engine/core/error.h"
struct mh_session_options;
struct mh_loading_view;
struct mh_race_loop_options;
struct mh_race_scene_assets;

/*
 * Initialize borrowed services and zero mode-specific hooks/outputs. Callers
 * set their root, audio, hooks, completion and retained music afterward.
 */
void	mh_session_race_options(const struct mh_session_options *options,
	struct mh_loading_view *loading, struct mh_race_loop_options *race);

/*
 * NULL course music retains the decoded track choice. Invalid indices leave
 * the track unchanged; this does not select horns or take audio ownership.
 */
enum mh_error_code	mh_session_course_music(
	const struct mh_session_options *options,
	struct mh_race_scene_assets *assets, struct mh_error *error_out);
#endif

#ifndef MH_CLIENT_SESSION_LOADING_H
#define MH_CLIENT_SESSION_LOADING_H

#include "engine/core/error.h"
#include "engine/render/scene_loading.h"

struct mh_loading_view;
struct mh_settings;
struct mh_session_options;

/* Stack owner during synchronous preparation; root/options are borrowed. */
struct mh_session_loading {
	const char			*root;
	const struct mh_session_options	*options;
	struct mh_loading_view		*view;
	struct mh_race_load_observer	observer;
	unsigned int			next;
};

void	mh_session_loading_init(struct mh_session_loading *loading,
	const char *root, const struct mh_session_options *options);
void	mh_session_loading_close(struct mh_session_loading *loading);

/*
 * NULL loading disables presentation for asset-only callers. Resolve the course
 * first. NULL settings uses the scene loader's default profile/viewport.
 */
enum mh_error_code			 mh_session_loading_begin(
	struct mh_session_loading *loading, unsigned int track,
	unsigned int laps, struct mh_settings *settings,
	struct mh_error *error_out);
const struct mh_race_load_observer	*mh_session_loading_observer(
	struct mh_session_loading *loading);
enum mh_error_code			 mh_session_loading_stage(
	struct mh_session_loading *loading, enum mh_race_load_stage stage,
	struct mh_error *error_out);

#endif

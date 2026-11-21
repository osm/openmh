#ifndef MH_RACE_SCENE_LOADING_H
#define MH_RACE_SCENE_LOADING_H

#include "engine/core/error.h"

/* Completed race-load operations, in the original loading-screen order. */
enum mh_race_load_stage {
	MH_RACE_LOAD_WORLD,
	MH_RACE_LOAD_TRACK,
	MH_RACE_LOAD_CARS,
	MH_RACE_LOAD_AI,
	MH_RACE_LOAD_FONT
};

/*
 * Optional synchronous observer, borrowed only during loading. A failed
 * callback aborts preparation through the loader's ordinary cleanup path.
 * It must not alter loaded data or reenter the loader. No UI/clock is owned.
 */
struct mh_race_load_observer {
	void				*context;
	enum mh_error_code (*complete)(void *context,
		enum mh_race_load_stage	stage, struct mh_error *error_out);
};

enum mh_error_code	mh_race_load_complete(
	const struct mh_race_load_observer *observer,
	enum mh_race_load_stage stage, struct mh_error *error_out);

#endif

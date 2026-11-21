#ifndef MH_DEMO_RACE_INTERNAL_H
#define MH_DEMO_RACE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/demo/race.h"

/* Apply cached poses and membership to the caller's working copy. */
enum mh_error_code	mh_demo_race_apply_samples(struct mh_demo_race *race,
	int initial, struct mh_error *error_out);

/*
 * Private-copy stages, like engine/simulation/field_internal.h. The owner
 * handles pause and publishes only after both stages succeed. Physics includes
 * recorded pose application; capture belongs between these two calls. Failure
 * may mutate the private input. No allocation or file IO.
 */
enum mh_error_code	mh_demo_race_frame_physics(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out);
enum mh_error_code	mh_demo_race_frame_finish(struct mh_demo_race *race,
	struct mh_error *error_out);

#endif

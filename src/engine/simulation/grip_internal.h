#ifndef MH_GRIP_INTERNAL_H
#define MH_GRIP_INTERNAL_H

#include "engine/simulation/grip.h"

/* Original road return: wrapper normalization follows all body contacts. */
enum mh_error_code	mh_grip_road_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	const struct mh_suspension_config *suspension, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out);

#endif

#ifndef MH_SCENE_INTERPOLATION_H
#define MH_SCENE_INTERPOLATION_H

#include "engine/simulation/field.h"

/*
 * Presentation copy only. Never publish this output to gameplay or recording.
 * Inputs and output must be distinct. Discontinuities snap to current state.
 */
enum mh_error_code	mh_scene_interpolate(const struct mh_field *previous,
	const struct mh_field *current, double alpha, struct mh_field *output,
	struct mh_error *error_out);

#endif

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/vehicle_placement.h"

/* Original binary32(.32), widened exactly for the native double pose. */
#define DISPLAY_ORIGIN_OFFSET 0.31999999284744262695

enum mh_error_code
mh_vehicle_place(const struct mh_engine_config *config,
	const struct mh_placement *placement, struct mh_vehicle_state *output,
	struct mh_error *error_out)
{
	double physics_position[3];
	unsigned int i;

	if (placement == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing vehicle placement");

	for (i = 0; i < 3; i++)
		physics_position[i] = placement->position[i] -
			DISPLAY_ORIGIN_OFFSET * placement->rotation[3 + i];

	return mh_vehicle_reset(config, physics_position, placement->rotation,
		output, error_out);
}

enum mh_error_code
mh_vehicle_relocate(const struct mh_engine_config *config,
	const struct mh_placement *placement,
	const struct mh_vehicle_state *state, struct mh_vehicle_state *output,
	struct mh_error *error_out)
{
	struct mh_vehicle_state fresh, result;
	enum mh_error_code status;

	if (state == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing vehicle relocation state");

	status = mh_vehicle_place(config, placement, &fresh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result = *state;
	result.motion = fresh.motion;
	result.engine = fresh.engine;
	result.engine.detached = state->engine.detached;
	result.grip.phase = 0;
	result.grip.slide_amount = 0;
	result.body.strong_air_damping = 0;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

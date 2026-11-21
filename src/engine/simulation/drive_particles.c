#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/drive.h"
#include "engine/simulation/drive_particles.h"

#define INPUT_LIMIT 1e6

enum mh_error_code
mh_drive_particles_emit(const struct mh_drive *drive,
	struct mh_particle_system *system, struct mh_particles *particles,
	struct mh_error *error_out)
{
	struct mh_particle_input input;
	struct mh_body_sparks sparks;
	double position[3], velocity[3], value, points[MH_CAR_BODY_POINTS][3];
	unsigned int component, axis, channel, corner;
	enum mh_error_code status;

	if (drive == NULL || drive->car == NULL ||
		(drive->remote != 0 && drive->remote != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid particle car");

	status = mh_motion_display(
		&drive->vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&input, 0, sizeof(input));
	sparks = drive->body_sparks;
	input.force_smoke = (drive->cheats & MH_CHEAT_UNDERWATER) != 0 &&
		!(drive->cheats & MH_CHEAT_THUNDER);

	for (component = 0; component < 9; component++)
		input.rotation[component] =
			(float)drive->vehicle.motion.rotation[component];

	for (axis = 0; axis < 3; axis++)
		input.position[axis] = (float)position[axis];

	for (channel = 0; channel < 2; channel++) {
		input.tyre_id[channel] =
			drive->vehicle.grip.tyre_effects.id[channel];
		input.spark_id[channel] = sparks.id[channel];
		input.spark_corner[channel] = sparks.corner[channel];

		if (!(sparks.gain[channel] >= -INPUT_LIMIT &&
			sparks.gain[channel] <= INPUT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid particle spark gain");

		input.spark_gain[channel] = (float)sparks.gain[channel];

		for (axis = 0; axis < 3; axis++) {
			value = drive->car->wheel_position[channel + 2][axis];

			if (!(value >= -INPUT_LIMIT && value <= INPUT_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid particle wheel attachment");

			input.wheels[channel][axis] = (float)value;
		}
	}

	status = mh_car_body_points(drive->car, points, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (corner = 0; corner < MH_CAR_BODY_POINTS; corner++)
		for (axis = 0; axis < 3; axis++) {
			value = points[corner][axis];

			if (!(value >= -INPUT_LIMIT && value <= INPUT_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid particle body corner");

			input.corners[corner][axis] = (float)value;
		}

	return mh_particles_emit(&input, system, particles, error_out);
}

#include <string.h>

#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/game/cheats.h"
#include "engine/simulation/suspension.h"
#include "engine/simulation/suspension_internal.h"

#define LENGTH_LIMIT 10000
#define COORDINATE_LIMIT 10000
#define MASS_LIMIT 10000
#define FORCE_LIMIT 1e9
#define POSITION_LIMIT 1e6
#define ORDINARY_DAMPING 65000
#define DEMON_LENGTH_SCALE 3.0
#define DEMON_STRENGTH_SCALE .16
#define DEMON_DAMPING 21666
#define G_RIDE_LENGTH_SCALE 1.2
#define G_RIDE_STRENGTH_SCALE .5
#define G_RIDE_DAMPING -50000

static int
bounded(double value, double limit)
{
	return value >= -limit && value <= limit;
}

static int
valid_config(const struct mh_suspension_config *config)
{
	return config->length > 0 && config->length <= LENGTH_LIMIT &&
		config->strength >= 0 && config->strength <= FORCE_LIMIT &&
		bounded(config->damping, FORCE_LIMIT);
}

enum mh_error_code
mh_suspension_configure(const struct mh_car *car, unsigned int flags,
	struct mh_suspension_config *output, struct mh_error *error_out)
{
	struct mh_suspension_config result;
	float length, strength;
	double length_scale, strength_scale;

	if (car == NULL || output == NULL || !car->has_suspension ||
		!(car->spring_length > 0 &&
			car->spring_length <= LENGTH_LIMIT) ||
		!(car->spring_strength >= 0 &&
			car->spring_strength <= FORCE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car suspension settings");

	result.length = car->spring_length;
	result.strength = car->spring_strength;
	result.damping = ORDINARY_DAMPING;

	if (flags & (MH_CHEAT_DEMON | MH_CHEAT_G_RIDE)) {
		length = (float)car->spring_length;
		strength = (float)car->spring_strength;
		length_scale = flags & MH_CHEAT_DEMON ? DEMON_LENGTH_SCALE
						: G_RIDE_LENGTH_SCALE;
		strength_scale = flags & MH_CHEAT_DEMON ? DEMON_STRENGTH_SCALE
							: G_RIDE_STRENGTH_SCALE;
		result.length = (float)((long double)length * length_scale);
		result.strength =
			(float)((long double)strength * strength_scale);
		result.damping =
			flags & MH_CHEAT_DEMON ? DEMON_DAMPING : G_RIDE_DAMPING;
	}

	if (!valid_config(&result))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"suspension configuration exceeds native bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
evaluate_wheel(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input,
	const struct mh_suspension *previous, unsigned int wheel_index,
	struct mh_suspension_wheel *wheel, struct mh_error *error_out)
{
	const double *rotation;
	volatile double attachment;
	double spring_delta;
	unsigned int axis;
	enum mh_error_code status;

	rotation = input->rotation;

	/*
	 * Binary32 pose/attachment inputs, 53-bit sums, then separate binary32
	 * stores for the transformed attachment, spring vector and both
	 * endpoints. Rounding just the final endpoints changes wheel contact
	 * decisions.
	 */
	for (axis = 0; axis < 3; axis++) {
		attachment =
			(double)(float)car->wheel_position[wheel_index][1] *
			(double)(float)rotation[3 + axis];
		attachment +=
			(double)(float)car->wheel_position[wheel_index][0] *
			(double)(float)rotation[axis];
		attachment +=
			(double)(float)car->wheel_position[wheel_index][2] *
			(double)(float)rotation[6 + axis];
		attachment += (double)(float)input->position[axis];
		attachment = (float)attachment;
		spring_delta = (float)(-(double)(float)config->length *
			(double)(float)rotation[3 + axis]);
		wheel->start[axis] = (float)(attachment - spring_delta);
		wheel->end[axis] = (float)(attachment + spring_delta);

		if (!bounded(wheel->start[axis], POSITION_LIMIT) ||
			!bounded(wheel->end[axis], POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid suspension query endpoints");
	}

	if (previous != NULL && previous->wheels[wheel_index].supported) {
		wheel->contact = previous->wheels[wheel_index].contact;
		mh_suspension_reuse_contact(
			wheel->start, wheel->end, &wheel->contact);
	} else {
		status = mh_collision_cast(col, wheel->start, wheel->end,
			&wheel->contact, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_suspension_apply_contact(car, config, input, wheel_index, wheel);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_suspension_evaluate(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_suspension_input *input, struct mh_suspension *output,
	struct mh_error *error_out)
{
	return mh_suspension_evaluate_configured(
		car, col, NULL, input, output, error_out);
}

enum mh_error_code
mh_suspension_evaluate_configured(const struct mh_car *car,
	const struct mh_collision *col,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input, struct mh_suspension *output,
	struct mh_error *error_out)
{
	return mh_suspension_evaluate_cached(
		car, col, config, input, NULL, output, error_out);
}

static double
stored_plane_distance(const struct mh_contact *contact, const double point[3])
{
	volatile double distance;
	unsigned int axis;

	/* Add the plane distance before the Y and Z products in double. */
	distance = (double)(float)contact->normal[0] * (double)(float)point[0];
	distance += (double)(float)contact->plane_distance;

	for (axis = 1; axis < 3; axis++)
		distance += (double)(float)contact->normal[axis] *
			(double)(float)point[axis];

	return distance;
}

void
mh_suspension_reuse_contact(
	const double start[3], const double end[3], struct mh_contact *contact)
{
	const double bias = (double)(float)0.08;
	double start_distance, end_distance, offset;
	volatile double denominator, fraction;

	/*
	 * Distances/fraction have binary32 stores; comparisons use binary64
	 * +/-0.08. Do not test boundaries or reconstruct the contact position.
	 */
	contact->hit = 0;
	contact->polygon_tests = 0;
	start_distance = (float)stored_plane_distance(contact, start);
	offset = 0;

	if (start_distance <= 0 && start_distance > -0.08) {
		offset = bias;
		start_distance = (float)(start_distance + bias);
	}

	if (start_distance <= 0)
		return;

	end_distance = (float)(stored_plane_distance(contact, end) + offset);

	if (end_distance > 0 && end_distance < 0.08)
		end_distance = (float)(end_distance - bias);

	if (end_distance > 0)
		return;

	denominator = start_distance - end_distance;
	fraction = start_distance / denominator;
	contact->fraction = (float)fraction;
	contact->hit = 1;
}

enum mh_error_code
mh_suspension_evaluate_cached(const struct mh_car *car,
	const struct mh_collision *col,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input,
	const struct mh_suspension *previous, struct mh_suspension *output,
	struct mh_error *error_out)
{
	struct mh_suspension result;
	struct mh_suspension_config ordinary;
	struct mh_suspension_wheel *wheel;
	enum mh_error_code status;
	const double *rotation;
	unsigned int wheel_index, axis;

	if (car == NULL || col == NULL || input == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid suspension arguments");

	if (!car->has_suspension || !(car->mass > 0 && car->mass <= MASS_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car suspension settings");

	if (config == NULL) {
		status = mh_suspension_configure(car, 0, &ordinary, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		config = &ordinary;
	}

	if (!valid_config(config))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid suspension configuration");

	rotation = input->rotation;

	if (!mh_rotation_valid(rotation))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"suspension requires an orthonormal rotation");

	for (axis = 0; axis < 3; axis++) {
		if (!bounded(input->position[axis], POSITION_LIMIT) ||
			!bounded(car->center_mass[axis], COORDINATE_LIMIT)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid suspension position or center of mass");
		}
	}

	for (wheel_index = 0; wheel_index < 4; wheel_index++) {
		if (!(input->previous_extension[wheel_index] >= 0 &&
			input->previous_extension[wheel_index] <= 1)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid previous suspension extension");
		}

		for (axis = 0; axis < 3; axis++) {
			if (!bounded(car->wheel_position[wheel_index][axis],
				COORDINATE_LIMIT) ||
				!bounded(input->traction[wheel_index][axis],
					FORCE_LIMIT) ||
				!bounded(input->applied[wheel_index][axis],
					FORCE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid wheel position or force");
		}
	}

	memset(&result, 0, sizeof(result));

	for (wheel_index = 0; wheel_index < 4; wheel_index++) {
		wheel = &result.wheels[wheel_index];
		status = evaluate_wheel(car, col, config, input, previous,
			wheel_index, wheel, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_suspension_accumulate(wheel, result.force, result.torque);
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

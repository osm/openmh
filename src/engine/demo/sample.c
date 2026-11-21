#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/sample.h"

#define WORLD_POSITION_LIMIT 1000000
#define ANGLE_WRAP_THRESHOLD 4
#define ANGLE_TURN 6.283185482025146484375L
#define MILLISECONDS_TO_SECONDS .001

static double
interpolate_float(double newer, double older, long double ratio)
{
	return (double)(float)(((long double)newer - (long double)older) *
			ratio +
		(long double)older);
}

static double
add_float_turn(double angle)
{
	return (double)(float)((long double)angle + ANGLE_TURN);
}

/* Pose deltas retain the original intermediate stores before derivatives. */
static void
interpolate_pose(const struct mh_net_vehicle *older,
	const struct mh_net_vehicle *newer, long double stored_ratio,
	long double seconds, struct mh_demo_sample *sample)
{
	struct mh_net_vehicle *vehicle;
	double newer_angle, older_angle, angle_delta, position_delta;
	long truncated_angle_delta;
	unsigned int axis;

	vehicle = &sample->vehicle;

	for (axis = 0; axis < 3; axis++) {
		vehicle->position[axis] =
			interpolate_float(newer->position[axis],
				older->position[axis], stored_ratio);
		position_delta = (double)((long double)newer->position[axis] -
			older->position[axis]);
		sample->world_velocity[axis] =
			(double)(float)((long double)position_delta / seconds);
		newer_angle = newer->angles[axis];
		older_angle = older->angles[axis];
		truncated_angle_delta = (long)(newer_angle - older_angle);

		/*
		 * Original uses abs(trunc(delta)) > pi, not fabs(delta) > pi.
		 */
		if (truncated_angle_delta >= ANGLE_WRAP_THRESHOLD ||
			truncated_angle_delta <= -ANGLE_WRAP_THRESHOLD) {
			if (newer_angle < older_angle)
				newer_angle = add_float_turn(newer_angle);
			else
				older_angle = add_float_turn(older_angle);
		}

		angle_delta =
			(double)(float)((long double)newer_angle - older_angle);
		vehicle->angles[axis] =
			(double)(float)((long double)older_angle +
				(long double)angle_delta * stored_ratio);
		sample->euler_rate[axis] =
			(double)(float)((long double)angle_delta / seconds);
	}
}

enum mh_error_code
mh_demo_sample_pair(const struct mh_demo_frame *older,
	const struct mh_demo_frame *newer, unsigned int slot,
	unsigned long elapsed_ms, struct mh_demo_sample *output,
	struct mh_error *error_out)
{
	struct mh_net_vehicle newer_vehicle, older_vehicle;
	struct mh_demo_sample result;
	struct mh_net_vehicle *vehicle;
	enum mh_error_code status;
	long double ratio, stored_ratio, seconds;
	unsigned long duration_ms;
	unsigned int i;

	if (older == NULL || newer == NULL || output == NULL ||
		slot >= MH_DEMO_MAX_PLAYERS ||
		newer->milliseconds > MH_U32_MAX ||
		older->milliseconds >= newer->milliseconds ||
		elapsed_ms < older->milliseconds ||
		elapsed_ms >= newer->milliseconds ||
		older->vehicles[slot] == NULL || newer->vehicles[slot] == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo interpolation pair");

	status = mh_net_vehicle_decode(older->vehicles[slot],
		MH_DEMO_VEHICLE_BYTES, &older_vehicle, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_vehicle_decode(newer->vehicles[slot],
			MH_DEMO_VEHICLE_BYTES, &newer_vehicle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++)
		if (!(older_vehicle.position[i] >= -WORLD_POSITION_LIMIT &&
			older_vehicle.position[i] <= WORLD_POSITION_LIMIT &&
			newer_vehicle.position[i] >=
				-WORLD_POSITION_LIMIT &&
			newer_vehicle.position[i] <= WORLD_POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"demo position outside world bounds");

	memset(&result, 0, sizeof(result));
	vehicle = &result.vehicle;
	*vehicle = older_vehicle;
	result.active = (older->active_mask & (1U << slot)) != 0;
	result.unavailable = (older->unavailable_mask & (1U << slot)) != 0;
	duration_ms = newer->milliseconds - older->milliseconds;
	ratio = (long double)(elapsed_ms - older->milliseconds) /
		(long double)(float)duration_ms;

	/*
	 * Suspension keeps the extended ratio; later channels use its float
	 * store.
	 */
	stored_ratio = (long double)(float)ratio;
	seconds =
		(long double)duration_ms * (long double)MILLISECONDS_TO_SECONDS;

	for (i = 0; i < 4; i++) {
		vehicle->extension[i] =
			interpolate_float(newer_vehicle.extension[i],
				older_vehicle.extension[i], ratio);
		vehicle->auxiliary[i] =
			interpolate_float(newer_vehicle.auxiliary[i],
				older_vehicle.auxiliary[i], stored_ratio);
	}

	vehicle->steering = interpolate_float(
		newer_vehicle.steering, older_vehicle.steering, stored_ratio);
	vehicle->rpm = interpolate_float(
		newer_vehicle.rpm, older_vehicle.rpm, stored_ratio);
	vehicle->slide = interpolate_float(
		newer_vehicle.slide, older_vehicle.slide, stored_ratio);

	for (i = 0; i < 2; i++)
		vehicle->wheel_speed[i] =
			interpolate_float(newer_vehicle.wheel_speed[i],
				older_vehicle.wheel_speed[i], stored_ratio);

	interpolate_pose(
		&older_vehicle, &newer_vehicle, stored_ratio, seconds, &result);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

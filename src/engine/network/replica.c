#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/pose.h"
#include "engine/network/replica.h"
#include "engine/network/vehicle_wire.h"

/* Native bounds permit finite extrapolation beyond the packed channels. */
#define CHANNEL_LIMIT 1000000
#define SPEED_KMH_SCALE 3.6

static int
channel_in_range(double value)
{
	return value >= -CHANNEL_LIMIT && value <= CHANNEL_LIMIT;
}

static void
world_to_body(
	const double rotation[9], const double vector[3], double output[3])
{
	long double value;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		value = (long double)(float)vector[1] * rotation[i * 3 + 1] +
			(long double)(float)vector[0] * rotation[i * 3];
		value += (long double)(float)vector[2] * rotation[i * 3 + 2];
		output[i] = (double)(float)value;
	}
}

static void
store_channels(
	const struct mh_net_vehicle *vehicle, struct mh_net_vehicle *stored)
{
	unsigned int i;

	*stored = *vehicle;

	for (i = 0; i < 4; i++) {
		stored->extension[i] = (double)(float)vehicle->extension[i];
		stored->auxiliary[i] = (double)(float)vehicle->auxiliary[i];
	}

	for (i = 0; i < 3; i++) {
		stored->angles[i] = (double)(float)vehicle->angles[i];
		stored->position[i] = (double)(float)vehicle->position[i];
	}

	for (i = 0; i < 2; i++)
		stored->wheel_speed[i] = (double)(float)vehicle->wheel_speed[i];

	stored->steering = (double)(float)vehicle->steering;
	stored->rpm = (double)(float)vehicle->rpm;
	stored->slide = (double)(float)vehicle->slide;
}

enum mh_error_code
mh_net_replica_update(const struct mh_net_vehicle *vehicle,
	const double world_velocity[3], const double euler_rate[3],
	double previous_pedal, struct mh_net_replica *output,
	struct mh_error *error_out)
{
	struct mh_net_replica result;
	enum mh_error_code status;
	unsigned int i;

	if (vehicle == NULL || world_velocity == NULL || euler_rate == NULL ||
		output == NULL || !(previous_pedal >= 0 && previous_pedal <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remote car arguments");

	if (!channel_in_range(vehicle->steering) ||
		!channel_in_range(vehicle->rpm) ||
		!channel_in_range(vehicle->slide) || vehicle->gear > 7 ||
		vehicle->flags > VEHICLE_FLAGS_MASK || vehicle->codes[0] > 7 ||
		vehicle->codes[1] > 7 || vehicle->codes[2] > 1 ||
		vehicle->codes[3] > 1 || vehicle->variants[0] > 3 ||
		vehicle->variants[1] > 3)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remote car channels");

	for (i = 0; i < 4; i++)
		if (!channel_in_range(vehicle->extension[i]) ||
			!channel_in_range(vehicle->auxiliary[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remote suspension or effect");

	for (i = 0; i < 3; i++)
		if (!channel_in_range(vehicle->position[i]) ||
			!channel_in_range(world_velocity[i]) ||
			!channel_in_range(euler_rate[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remote position or derivative");

	for (i = 0; i < 2; i++)
		if (!channel_in_range(vehicle->wheel_speed[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remote wheel rate");

	memset(&result, 0, sizeof(result));
	status = mh_pose_matrix(vehicle->angles, result.rotation, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	store_channels(vehicle, &result.vehicle);
	world_to_body(result.rotation, world_velocity, result.velocity);
	world_to_body(result.rotation, euler_rate, result.angular_velocity);
	result.speed = (double)(float)((long double)fabs(result.velocity[2]) *
		(long double)SPEED_KMH_SCALE);
	result.sound_pedal = vehicle->flags & VEHICLE_PEDAL_FLAG
		? 1
		: (double)(float)previous_pedal;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

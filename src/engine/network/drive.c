#include <string.h>

#include "engine/core/error.h"
#include "engine/core/pose.h"
#include "engine/network/drive.h"
#include "engine/network/vehicle_wire.h"
#include "engine/simulation/motion_internal.h"

/* Original display/physics and presentation-ray local-Y offsets. */
#define DISPLAY_HEIGHT 0.319999992847442626953125
#define CONTACT_ORIGIN_SHIFT (-.25L)
#define CAR_GEOMETRY_LIMIT 10000

static int
is_boolean(int value)
{
	return value == 0 || value == 1;
}

enum mh_error_code
mh_net_drive_capture_remote(const struct mh_drive *drive,
	const struct mh_net_vehicle *cached, struct mh_net_vehicle *output,
	struct mh_error *error_out)
{
	struct mh_net_vehicle result;
	unsigned char encoded[MH_NET_VEHICLE_BYTES];
	enum mh_error_code status;

	if (drive == NULL || cached == NULL || output == NULL ||
		drive->remote != 1 ||
		!(drive->sound_pedal >= 0 && drive->sound_pedal <= 1) ||
		cached->flags > VEHICLE_FLAGS_MASK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remote recording car");

	result = *cached;

	/*
	 * Retain sound_pedal when incoming bit 10 is zero. Recording tests that
	 * applied value.
	 */
	result.flags = (result.flags & VEHICLE_BELOW_PEDAL_MASK) |
		(drive->sound_pedal > 0 ? VEHICLE_PEDAL_FLAG : 0);
	status = mh_pose_angles(
		drive->vehicle.motion.rotation, result.angles, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_vehicle_encode(
			&result, encoded, sizeof(encoded), error_out);

	if (status == MH_ERROR_NONE)
		*output = result;

	return status;
}

static enum mh_error_code
capture_effects(const struct mh_drive *drive,
	const struct mh_net_drive_effects *effects,
	struct mh_net_vehicle *snapshot, struct mh_error *error_out)
{
	const struct mh_vehicle_state *vehicle;
	struct mh_body_sparks sparks;
	unsigned int i;

	vehicle = &drive->vehicle;

	if (effects != NULL) {
		memcpy(snapshot->auxiliary, effects->auxiliary,
			sizeof(snapshot->auxiliary));
		memcpy(snapshot->codes, effects->codes,
			sizeof(snapshot->codes));
		memcpy(snapshot->variants, effects->variants,
			sizeof(snapshot->variants));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	sparks = drive->body_sparks;

	for (i = 0; i < 2; i++) {
		if (vehicle->grip.tyre_effects.id[i] > 7 || sparks.id[i] > 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network effect ID");

		snapshot->codes[i] =
			(unsigned int)vehicle->grip.tyre_effects.id[i];
		snapshot->codes[i + 2] = (unsigned int)sparks.id[i];
		snapshot->auxiliary[i] = vehicle->grip.tyre_effects.gain[i];
		snapshot->auxiliary[i + 2] = sparks.gain[i];
		snapshot->variants[i] = sparks.corner[i];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_drive_capture(const struct mh_drive *drive,
	const struct mh_net_drive_effects *effects,
	struct mh_net_vehicle *output, struct mh_error *error_out)
{
	struct mh_net_vehicle result;
	const struct mh_vehicle_state *vehicle;
	unsigned char encoded[MH_NET_VEHICLE_BYTES];
	double velocity[3];
	unsigned int i;
	enum mh_error_code status;

	if (drive == NULL || output == NULL || drive->remote ||
		!(drive->sound_pedal >= 0 && drive->sound_pedal <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local network car");

	vehicle = &drive->vehicle;

	if (!is_boolean(vehicle->brake_active) ||
		!is_boolean(vehicle->grip.sliding) ||
		!is_boolean(vehicle->wheelspin) ||
		!is_boolean(vehicle->heavy_brake) ||
		!is_boolean(vehicle->engine_braking) ||
		!is_boolean(drive->horn))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid local car flags");

	memset(&result, 0, sizeof(result));
	status = mh_motion_display(
		&vehicle->motion, result.position, velocity, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_pose_angles(
			vehicle->motion.rotation, result.angles, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(result.extension, vehicle->motion.extension,
		sizeof(result.extension));
	memcpy(result.wheel_speed, vehicle->wheel_rate,
		sizeof(result.wheel_speed));
	result.steering = vehicle->smoothed_steering;
	result.rpm = vehicle->engine.rpm;
	result.slide = vehicle->grip.slide_amount;
	result.gear = vehicle->engine.gear;
	result.flags =
		((unsigned int)vehicle->brake_active << VEHICLE_BRAKE_SHIFT) |
		((unsigned int)drive->horn << VEHICLE_HORN_SHIFT) |
		((unsigned int)vehicle->grip.sliding << VEHICLE_SLIDING_SHIFT) |
		((unsigned int)vehicle->wheelspin << VEHICLE_WHEELSPIN_SHIFT) |
		((unsigned int)vehicle->heavy_brake
			<< VEHICLE_HEAVY_BRAKE_SHIFT) |
		((unsigned int)vehicle->engine_braking
			<< VEHICLE_ENGINE_BRAKE_SHIFT) |
		(drive->sound_pedal > 0 ? VEHICLE_PEDAL_FLAG : 0);

	for (i = 0; i < 4; i++) {
		if (!is_boolean(vehicle->grip.wheels[i].supported))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid local wheel support");

		result.flags |= (unsigned int)vehicle->grip.wheels[i].supported
			<< (VEHICLE_SUPPORT_SHIFT + i);
	}

	status = capture_effects(drive, effects, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_net_vehicle_encode(
		&result, encoded, sizeof(encoded), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
query_contacts(const struct mh_car *car, const struct mh_collision *col,
	struct mh_net_drive_sample *sample, struct mh_error *error_out)
{
	const double *rotation, *position, *attachment;
	double origin[3], spring[3], center;
	long double value;
	unsigned int wheel, axis, component;
	enum mh_error_code status;

	if (car == NULL || col == NULL ||
		!(car->spring_length > 0 &&
			car->spring_length <= CAR_GEOMETRY_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing remote car contact assets");

	rotation = sample->replica.rotation;
	position = sample->replica.vehicle.position;

	for (axis = 0; axis < 3; axis++) {
		origin[axis] = (double)(float)((long double)rotation[3 + axis] *
				CONTACT_ORIGIN_SHIFT +
			position[axis]);
		spring[axis] = (double)(float)((long double)rotation[3 + axis] *
			-(float)car->spring_length);
	}

	for (wheel = 0; wheel < 4; wheel++) {
		attachment = car->wheel_position[wheel];

		for (component = 0; component < 3; component++)
			if (!(attachment[component] >= -CAR_GEOMETRY_LIMIT &&
				attachment[component] <=
					CAR_GEOMETRY_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid remote wheel attachment");

		for (axis = 0; axis < 3; axis++) {
			value = (long double)(float)attachment[1] *
					rotation[3 + axis] +
				(long double)(float)attachment[0] *
					rotation[axis];

			value += (long double)(float)attachment[2] *
				rotation[6 + axis];

			center = (double)(float)(value + origin[axis]);
			sample->end[wheel][axis] =
				(double)(float)(center + spring[axis]);
			sample->start[wheel][axis] =
				(double)(float)(center - spring[axis]);
		}

		status = mh_collision_cast(col, sample->start[wheel],
			sample->end[wheel], &sample->contacts[wheel],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!sample->contacts[wheel].hit) {
			sample->contacts[wheel].normal[1] = 1;
			memcpy(sample->contacts[wheel].position,
				sample->end[wheel], sizeof(sample->end[wheel]));
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_motion(struct mh_drive *drive, const struct mh_net_replica *replica,
	struct mh_error *error_out)
{
	struct mh_vehicle_state *state;
	const struct mh_net_vehicle *vehicle;
	unsigned int i;

	state = &drive->vehicle;
	vehicle = &replica->vehicle;

	memcpy(state->motion.rotation, replica->rotation,
		sizeof(state->motion.rotation));
	memcpy(state->motion.velocity, replica->velocity,
		sizeof(state->motion.velocity));
	memcpy(state->motion.angular_velocity, replica->angular_velocity,
		sizeof(state->motion.angular_velocity));

	/*
	 * Native history is unused for remote forces; keep it bounded/coherent.
	 */
	memcpy(state->motion.previous_velocity, state->motion.velocity,
		sizeof(state->motion.previous_velocity));
	memcpy(state->motion.previous_angular_velocity,
		state->motion.angular_velocity,
		sizeof(state->motion.previous_angular_velocity));

	for (i = 0; i < 3; i++)
		state->motion.position[i] = vehicle->position[i] -
			DISPLAY_HEIGHT * state->motion.rotation[3 + i];

	for (i = 0; i < 4; i++) {
		drive->remote_extension[i] = vehicle->extension[i];

		if (vehicle->extension[i] < 0)
			state->motion.extension[i] = 0;
		else if (vehicle->extension[i] > 1)
			state->motion.extension[i] = 1;
		else
			state->motion.extension[i] = vehicle->extension[i];

		state->grip.wheels[i].supported =
			(vehicle->flags >> (VEHICLE_SUPPORT_SHIFT + i)) & 1;
	}

	if (!mh_motion_valid(&state->motion))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"remote pose exceeds native motion limits");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_drive_apply(struct mh_drive *drive,
	const struct mh_net_playback *playback,
	struct mh_net_drive_sample *sample, struct mh_error *error_out)
{
	struct mh_drive next;
	struct mh_net_drive_sample result;
	struct mh_vehicle_state *state;
	const struct mh_net_vehicle *vehicle;
	unsigned int i;
	enum mh_error_code status;

	if (drive == NULL || playback == NULL || sample == NULL ||
		drive->ai_path != NULL || !is_boolean(playback->ready) ||
		!is_boolean(playback->updated))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid remote playback");

	if (!playback->ready || !playback->updated)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&result, 0, sizeof(result));
	next = *drive;
	state = &next.vehicle;
	status = mh_net_replica_update(&playback->vehicle,
		playback->world_velocity, playback->euler_rate,
		drive->sound_pedal, &result.replica, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	vehicle = &result.replica.vehicle;
	status = apply_motion(&next, &result.replica, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = query_contacts(drive->car, drive->col, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 4; i++) {
		state->grip.wheels[i].material = result.contacts[i].material;
		state->grip.wheels[i].normal_y = result.contacts[i].normal[1];
		state->wheel_contacts[i] = result.contacts[i];
	}

	/*
	 * Save the received pose after the local-Y -.32 shift, before skipping
	 * forces for a remote car.
	 */
	state->contact_origin_y = state->motion.position[1];
	state->smoothed_steering = vehicle->steering;
	state->engine.rpm = vehicle->rpm;
	state->engine.gear = vehicle->gear;
	state->speed = (float)result.replica.speed;
	memcpy(state->wheel_rate, vehicle->wheel_speed,
		sizeof(state->wheel_rate));
	state->grip.sliding = (vehicle->flags >> VEHICLE_SLIDING_SHIFT) & 1;
	state->grip.slide_amount = vehicle->slide;
	state->brake_active = (vehicle->flags >> VEHICLE_BRAKE_SHIFT) & 1;
	state->wheelspin = (vehicle->flags >> VEHICLE_WHEELSPIN_SHIFT) & 1;
	state->heavy_brake = (vehicle->flags >> VEHICLE_HEAVY_BRAKE_SHIFT) & 1;
	state->engine_braking =
		(vehicle->flags >> VEHICLE_ENGINE_BRAKE_SHIFT) & 1;

	for (i = 0; i < 2; i++) {
		state->grip.tyre_effects.id[i] = vehicle->codes[i];
		state->grip.tyre_effects.gain[i] = vehicle->auxiliary[i];
		next.body_sparks.id[i] = vehicle->codes[i + 2];
		next.body_sparks.gain[i] = vehicle->auxiliary[i + 2];
		next.body_sparks.corner[i] = vehicle->variants[i];
	}

	next.sound_pedal = result.replica.sound_pedal;
	next.remote = 1;
	next.horn = (vehicle->flags >> VEHICLE_HORN_SHIFT) & 1;
	*drive = next;
	*sample = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

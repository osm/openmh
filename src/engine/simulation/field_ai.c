#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/field_internal.h"

#define HEADING_MIN_LENGTH 1e-6
#define TRAFFIC_WIDTH 2.1
#define TRAFFIC_LENGTH 4.4

enum mh_error_code
mh_field_locate(
	struct mh_field *field, unsigned int entry, struct mh_error *error_out)
{
	struct mh_ai_location location;
	double position[3], velocity[3];
	enum mh_error_code status;

	if (field->path == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (field->cars[entry].ai_path != NULL)
		field->point[entry] = field->cars[entry].ai.state.point;
	else {
		status = mh_motion_display(&field->cars[entry].vehicle.motion,
			position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_ai_locate(field->path, position[0],
				position[2], &location, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		field->point[entry] = location.point;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_ai_inputs(const struct mh_field *field, unsigned int reset_mask,
	struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	struct mh_error *error_out)
{
	const struct mh_drive *drive;
	struct mh_ai_roster_driver *driver;
	double position[3], velocity[3];
	unsigned int entry;
	enum mh_error_code status;

	if (field->count > MH_TRAFFIC_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ordinary race exceeds AI capacity");

	memset(drivers, 0, MH_TRAFFIC_MAX_CARS * sizeof(*drivers));

	for (entry = 0; entry < field->count; entry++) {
		drive = &field->cars[entry];
		driver = &drivers[entry];
		status = mh_motion_display(
			&drive->vehicle.motion, position, velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		driver->feedback.x = position[0];
		driver->feedback.z = position[2];
		driver->feedback.heading_x = drive->vehicle.motion.rotation[6];
		driver->feedback.heading_z = drive->vehicle.motion.rotation[8];
		driver->feedback.speed = drive->vehicle.motion.velocity[2];
		driver->feedback.wheelspin = drive->vehicle.wheelspin;
		driver->feedback.boosted =
			drive->ai_personality_enabled ? drive->ai_boosted : 0;
		driver->ai = drive->ai_path != NULL;
		driver->config = drive->opponent.driver;
		driver->personality_enabled = drive->ai_personality_enabled;
		driver->personality = drive->ai_personality;
		memcpy(driver->parameters, drive->ai_parameters,
			sizeof(driver->parameters));

		if (driver->ai)
			driver->state = drive->ai.state;
		else if (reset_mask & (1U << entry)) {
			status = mh_ai_driver_start(field->path, position[0],
				position[2], &driver->state, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else {
			driver->state.point = driver->state.corner =
				field->ai_roster.cars[entry].point;
			driver->state.lane = field->ai_roster.cars[entry].lane;
			driver->state.throttle_limit = 1;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_ai_reset(struct mh_field *field, struct mh_error *error_out)
{
	struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_roster history;
	enum mh_error_code status;

	if (field->path == NULL || field->network || field->playback)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_field_ai_inputs(
		field, field->active_mask, drivers, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ai_roster_start(field->path, drivers,
			field->active_mask, &history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	field->ai_roster = history;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_traffic_sample(const struct mh_field *field, unsigned int entry,
	struct mh_ai_traffic_car *output, struct mh_error *error_out)
{
	const struct mh_drive *drive;
	const struct mh_ai_point *point;
	struct mh_ai_cursor cursor;
	double position[3], velocity[3], dx, dz, lateral_squared,
		heading_length;
	enum mh_error_code status;

	drive = &field->cars[entry];
	status = mh_motion_display(
		&drive->vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ai_follow(field->path, position[0], position[2],
		field->point[entry], field->point[entry], &cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(output, 0, sizeof(*output));
	output->point = cursor.point;
	point = &field->path->points[cursor.point];
	dx = position[0] - point->x;
	dz = position[2] - point->z;
	output->along = dx * point->direction[0] + dz * point->direction[2];
	lateral_squared = dx * dx + dz * dz - output->along * output->along;
	output->lateral = lateral_squared > 0 ? sqrt(lateral_squared) : 0;

	if (dz * point->direction[0] - dx * point->direction[2] > 0)
		output->lateral = -output->lateral;

	heading_length = sqrt(drive->vehicle.motion.rotation[6] *
			drive->vehicle.motion.rotation[6] +
		drive->vehicle.motion.rotation[8] *
			drive->vehicle.motion.rotation[8]);

	if (!(heading_length >= HEADING_MIN_LENGTH))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"degenerate traffic heading");

	output->heading_dot =
		(drive->vehicle.motion.rotation[6] * point->direction[0] +
			drive->vehicle.motion.rotation[8] *
				point->direction[2]) /
		heading_length;
	output->speed = drive->vehicle.motion.velocity[2];
	output->lane = drive->ai.state.lane;
	output->lateral_rate = field->lateral_rate[entry];

	/* Original AI dimensions are the same for every car. */
	output->width = (double)(float)TRAFFIC_WIDTH;
	output->length = (double)(float)TRAFFIC_LENGTH;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

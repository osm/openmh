#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/field.h"
#include "engine/simulation/intro_camera.h"

#define CAMERA_TIME_LIMIT 10
#define DIAGNOSTIC_HZ 120.0

unsigned int
mh_field_viewed(const struct mh_field *field)
{
	if (field == NULL || field->viewed_entry >= field->count ||
		field->viewed_entry >= MH_RACE_MAX_CARS ||
		!(field->active_mask & (1U << field->viewed_entry)))
		return 0;

	return field->viewed_entry;
}

int
mh_field_sound_detailed(const struct mh_field *field, unsigned int entry)
{
	return field != NULL && entry < field->count &&
		entry < MH_RACE_MAX_CARS &&
		(field->listener_mask & (1U << entry)) != 0;
}

int
mh_field_listener_positional(const struct mh_field *field)
{
	return !mh_field_sound_detailed(field, mh_field_viewed(field));
}

static void
copy_camera(const struct mh_drive *previous, struct mh_drive *next)
{
	next->camera = previous->camera;
	next->camera_goal = previous->camera_goal;
	next->camera_intro = previous->camera_intro;
	next->camera_saved_in_car = previous->camera_saved_in_car;
	next->camera_mode = previous->camera_mode;
	next->look_back = previous->look_back;
	next->camera_rear_view = previous->camera_rear_view;
	next->camera_focal = previous->camera_focal;
	next->camera_far = previous->camera_far;
	memcpy(next->eye, previous->eye, sizeof(next->eye));
	memcpy(next->target, previous->target, sizeof(next->target));
	memcpy(next->forward, previous->forward, sizeof(next->forward));
}

enum mh_error_code
mh_field_restart_listener(struct mh_field *field, struct mh_error *error_out)
{
	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid restart listener field");

	/*
	 * Change the viewed slot without touching shared camera globals.
	 * Preserve other detailed listeners: this is not the F4 sound command.
	 */
	if (field->viewed_entry != 0)
		copy_camera(&field->cars[field->viewed_entry], &field->cars[0]);

	field->viewed_entry = 0;
	field->listener_mask |= 1;
	field->listener_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_select_view(
	struct mh_field *field, unsigned int entry, struct mh_error *error_out)
{
	const struct mh_drive *previous;
	struct mh_drive *next;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count || entry >= field->count ||
		field->active_mask == 0 ||
		field->active_mask >= (1U << field->count) ||
		!(field->active_mask & (1U << entry)))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid viewed car");

	if (entry == field->viewed_entry)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	previous = &field->cars[field->viewed_entry];
	next = &field->cars[entry];
	copy_camera(previous, next);
	field->listener_mask &= ~(1U << field->viewed_entry);

	if (!previous->camera_goal && previous->camera_mode != MH_CAMERA_TRACK)
		field->listener_mask |= 1U << entry;

	field->viewed_entry = entry;
	field->listener_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_cycle_view(struct mh_field *field, struct mh_error *error_out)
{
	unsigned int entry;
	enum mh_error_code status;

	if (field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing camera field");

	status = mh_camera_cycle(field->viewed_entry, field->count,
		field->active_mask, &entry, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (field->cars[field->viewed_entry].camera_goal) {
		field->cars[field->viewed_entry].camera_goal = 0;
		field->cars[field->viewed_entry].camera_intro = 0;
		field->cars[field->viewed_entry].camera_mode = MH_CAMERA_TRACK;
	}

	if (entry == field->viewed_entry) {
		if (field->cars[entry].camera_mode == MH_CAMERA_TRACK)
			field->listener_mask &= ~(1U << entry);
		else
			field->listener_mask |= 1U << entry;
		field->listener_revision++;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_field_select_view(field, entry, error_out);
}

enum mh_error_code
mh_field_camera_finish(
	struct mh_field *field, int local_trigger, struct mh_error *error_out)
{
	struct mh_drive *viewed;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count ||
		!(field->active_mask & (1U << field->viewed_entry)) ||
		(local_trigger != 0 && local_trigger != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish camera field");

	viewed = &field->cars[field->viewed_entry];

	if ((local_trigger || viewed->race.finished) && viewed->track != NULL &&
		viewed->track->has_goal_camera) {
		viewed->camera_goal = 1;
		viewed->camera_intro = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_camera_mode
mh_field_camera_preference(
	const struct mh_field *field, enum mh_camera_mode previous)
{
	const struct mh_drive *viewed;

	if (field == NULL || field->viewed_entry >= field->count ||
		field->attract)
		return previous;

	viewed = &field->cars[field->viewed_entry];

	/*
	 * Retain the prior camera mode for finish/track views; they request
	 * neither in-car nor chase mode.
	 */
	if (viewed->race.finished || viewed->camera_goal ||
		viewed->camera_mode == MH_CAMERA_TRACK)
		return previous;

	return viewed->camera_mode;
}

enum mh_error_code
mh_field_camera_mode(struct mh_field *field, enum mh_camera_mode mode,
	struct mh_error *error_out)
{
	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count ||
		!(field->active_mask & (1U << field->viewed_entry)) ||
		(mode != MH_CAMERA_CHASE && mode != MH_CAMERA_IN_CAR &&
			mode != MH_CAMERA_TRACK))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera mode command");

	field->cars[field->viewed_entry].camera_goal = 0;
	field->cars[field->viewed_entry].camera_intro = 0;
	field->cars[field->viewed_entry].camera_saved_in_car =
		mode == MH_CAMERA_IN_CAR;
	field->cars[field->viewed_entry].camera_mode = mode;

	if (mode == MH_CAMERA_TRACK)
		field->listener_mask &= ~(1U << field->viewed_entry);
	else
		field->listener_mask |= 1U << field->viewed_entry;
	field->listener_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_intro_camera(struct mh_field *field, struct mh_error *error_out)
{
	struct mh_drive *viewed;
	struct mh_intro_camera_state state;
	struct mh_intro_camera_input input;
	struct mh_intro_camera_result result;
	double channels[9], position[3], velocity[3];
	float eye[3], target[3];
	unsigned int sample, axis;
	enum mh_error_code status;

	viewed = &field->cars[field->viewed_entry];

	if (!viewed->camera_intro)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!field->cars[0].countdown || viewed->camera_goal) {
		viewed->camera_intro = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	state.enabled = 1;
	state.saved_in_car = viewed->camera_saved_in_car;
	state.requested_in_car = viewed->camera_mode == MH_CAMERA_IN_CAR;
	state.in_car = mh_drive_camera_in_car(viewed);
	state.chase = viewed->camera_mode == MH_CAMERA_CHASE;
	state.track = viewed->camera_mode == MH_CAMERA_TRACK;

	/*
	 * Local frames use the sampled absolute clock and original intro
	 * anchor. A held diagnostic/network countdown retains its first
	 * authored pose.
	 */
	input.start_ms =
		field->cars[0].frame.active ? field->cars[0].frame.intro_ms : 1;
	input.now_ms = field->cars[0].frame.active
		? mh_drive_clock_ms(&field->cars[0])
		: 1 +
			(double)field->cars[0].start_ticks * 1000.0 /
				DIAGNOSTIC_HZ;

	input.has_first_motion = viewed->start_camera[0] != NULL;
	input.has_second_motion = viewed->start_camera[1] != NULL;
	status = mh_intro_camera_step(&state, &input, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (result.count) {
		status = mh_motion_display(&field->cars[0].vehicle.motion,
			position, velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (axis = 0; axis < 3; axis++)
			target[axis] = (float)position[axis];

		for (sample = 0; sample < result.count; sample++) {
			status = mh_scene_channels(
				viewed->start_camera[result.samples[sample]
						.motion_index],
				result.samples[sample].frame, channels,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			for (axis = 0; axis < 3; axis++)
				eye[axis] = (float)channels[axis];

			status = mh_camera_intro_view(&viewed->camera, eye,
				target, &viewed->camera, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		for (axis = 0; axis < 3; axis++) {
			viewed->eye[axis] = viewed->camera.position[axis];
			viewed->forward[axis] =
				viewed->camera.rotation[6 + axis];
			viewed->target[axis] =
				viewed->eye[axis] + viewed->forward[axis];
		}
	}

	viewed->camera_intro = result.state.enabled;
	viewed->camera_mode = result.state.requested_in_car
		? MH_CAMERA_IN_CAR
		: (result.state.track ? MH_CAMERA_TRACK : MH_CAMERA_CHASE);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_camera_update(
	struct mh_field *field, float elapsed, struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count ||
		!(field->active_mask & (1U << field->viewed_entry)) ||
		!(elapsed >= 0 && elapsed <= CAMERA_TIME_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid camera field");

	next = *field;
	status = mh_drive_camera_update(
		&next.cars[next.viewed_entry], elapsed, error_out);

	if (status == MH_ERROR_NONE)
		status = apply_intro_camera(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

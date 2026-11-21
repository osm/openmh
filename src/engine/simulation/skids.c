#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/skids.h"

#define INPUT_LIMIT 1000000
#define HISTORY_LIMIT 10000000
#define VELOCITY_LIMIT 10000
#define BASIS_LIMIT 2
#define SLIDE_THRESHOLD 8
#define ATTACHMENT_WIDTH_SCALE 1.25L
#define IN_CAR_OFFSET .5L
#define CONTACT_Y_WEIGHT .333
#define SPACING_BASE 650
#define SPACING_SCALE .0015
#define MAX_COMMIT_DISTANCE_SQUARED 9
#define MAX_SEGMENT_DISTANCE_SQUARED 144
#define MIN_PARTIAL_DISTANCE_SQUARED .01

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

void
mh_skids_reset(struct mh_skids *state)
{
	if (state != NULL)
		memset(state, 0, sizeof(*state));
}

static int
valid_update(const struct mh_skid_input *input, const struct mh_skids *state)
{
	const struct mh_skid_wheel *wheel;
	const struct mh_skid_contact *contact;
	const struct mh_skid_segment *segment;
	unsigned int component, axis, wheel_index, segment_index;

	if (!valid_flag(input->wheelspin) || !valid_flag(input->heavy_brake) ||
		!valid_flag(input->viewed) || !valid_flag(input->disabled) ||
		!valid_flag(input->in_car) ||
		!bounded(input->contact_origin_y, INPUT_LIMIT) ||
		!bounded(input->velocity_x, VELOCITY_LIMIT) ||
		!bounded(input->velocity_z, VELOCITY_LIMIT) ||
		!bounded(input->slide_amount, INPUT_LIMIT))
		return 0;

	for (component = 0; component < 9; component++)
		if (!bounded(input->rotation[component], BASIS_LIMIT))
			return 0;

	for (axis = 0; axis < 3; axis++)
		if (!bounded(input->position[axis], INPUT_LIMIT))
			return 0;

	for (wheel_index = 0; wheel_index < 4; wheel_index++) {
		wheel = &state->wheels[wheel_index];
		contact = &input->wheels[wheel_index];

		if (!valid_flag(contact->supported) ||
			!valid_flag(wheel->active) ||
			!valid_flag(wheel->committed) ||
			!valid_flag(wheel->partial) ||
			!valid_flag(wheel->oldest_visible) ||
			wheel->first >= MH_SKID_SEGMENTS ||
			wheel->next >= MH_SKID_SEGMENTS ||
			wheel->previous >= MH_SKID_SEGMENTS ||
			wheel->count > MH_SKID_SEGMENTS)
			return 0;

		for (axis = 0; axis < 3; axis++)
			if (!bounded(contact->attachment[axis], INPUT_LIMIT) ||
				!bounded(
					contact->position[axis], INPUT_LIMIT) ||
				!bounded(contact->normal[axis], BASIS_LIMIT))
				return 0;

		for (segment_index = 0; segment_index < MH_SKID_SEGMENTS;
			segment_index++) {
			segment = &wheel->segments[segment_index];

			for (axis = 0; axis < 3; axis++)
				if (!bounded(segment->start[axis],
					HISTORY_LIMIT) ||
					!bounded(segment->end[axis],
						HISTORY_LIMIT) ||
					!bounded(segment->normal[axis],
						BASIS_LIMIT) ||
					!(segment->color[axis] >= 0 &&
						segment->color[axis] <= 255))
					return 0;
		}
	}

	return 1;
}

static void
discard_oldest(struct mh_skid_wheel *wheel)
{
	if (wheel->count == MH_SKID_SEGMENTS) {
		wheel->count--;
		wheel->first = (wheel->first + 1) % MH_SKID_SEGMENTS;
	}
}

static void
mark_color(unsigned long mark, float color[3])
{
	unsigned int channel;

	for (channel = 0; channel < 3; channel++)
		color[channel] = 0;

	if (mark == 3) {
		color[0] = 80;
		color[1] = 60;
		color[2] = 20;
	}

	if (mark == 4) {
		color[0] = 40;
		color[1] = 35;
		color[2] = 20;
	}
}

static void
contact_point(const struct mh_skid_input *input, unsigned int wheel_index,
	const struct mh_skid_contact *contact, float point[3])
{
	float local[3];
	unsigned int axis;

	local[0] = (float)((long double)contact->attachment[0] *
		ATTACHMENT_WIDTH_SCALE);
	local[1] = contact->attachment[1];
	local[2] = contact->attachment[2];

	/* The original offset includes the third wheel. */
	if (input->viewed && input->in_car && wheel_index < 3)
		local[2] = (float)((long double)local[2] - IN_CAR_OFFSET);

	for (axis = 0; axis < 3; axis++)
		point[axis] = (float)((long double)local[1] *
				input->rotation[axis + 3] +
			(long double)local[0] * input->rotation[axis] +
			(long double)local[2] * input->rotation[axis + 6] +
			input->position[axis]);

	point[1] = (float)(((long double)input->position[1] -
				input->contact_origin_y) *
			(long double)CONTACT_Y_WEIGHT +
		contact->position[1]);
}

static int
wheel_emits(const struct mh_skid_input *input, unsigned int wheel_index)
{
	if (wheel_index < 2)
		return (input->slide_amount > SLIDE_THRESHOLD &&
			!input->wheelspin) ||
			input->heavy_brake;

	return input->slide_amount > SLIDE_THRESHOLD || input->wheelspin ||
		input->heavy_brake;
}

static void
update_wheel(const struct mh_skid_input *input, unsigned int wheel_index,
	struct mh_skid_wheel *wheel)
{
	const struct mh_skid_contact *contact;
	struct mh_skid_segment *segment;
	float point[3], color[3], dx, distance_squared, commit_distance_squared;
	long double dz, spacing;
	int emit;

	contact = &input->wheels[wheel_index];
	emit = wheel_emits(input, wheel_index);

	if (input->disabled || !contact->supported || contact->mark < 1 ||
		contact->mark > 4 || !emit ||
		(!input->viewed && wheel->count == MH_SKID_SEGMENTS &&
			wheel->oldest_visible)) {
		wheel->active = wheel->committed = wheel->partial = 0;
		return;
	}

	mark_color(contact->mark, color);
	contact_point(input, wheel_index, contact, point);
	segment = &wheel->segments[wheel->next];

	if (!wheel->active) {
		memcpy(segment->start, point, sizeof(point));
		memcpy(segment->color, color, sizeof(color));
		wheel->active = 1;
		wheel->committed = wheel->partial = 0;
		discard_oldest(wheel);
		return;
	}

	if (wheel->committed) {
		memcpy(segment->start, wheel->segments[wheel->previous].end,
			sizeof(point));
		memcpy(segment->color, color, sizeof(color));
	}

	dx = (float)((long double)point[0] - segment->start[0]);
	dz = (long double)point[2] - segment->start[2];

	/*
	 * Z difference and spacing retain extended precision
	 * while also multiplying their explicitly stored binary32 copies.
	 */
	distance_squared = (float)((long double)dx * dx + dz * (float)dz);
	spacing = ((long double)input->velocity_x * input->velocity_x +
			(long double)input->velocity_z * input->velocity_z +
			SPACING_BASE) *
		(long double)SPACING_SCALE;
	commit_distance_squared = (float)(spacing * (float)spacing);

	if (commit_distance_squared > MAX_COMMIT_DISTANCE_SQUARED)
		commit_distance_squared = MAX_COMMIT_DISTANCE_SQUARED;

	wheel->active = 1;
	wheel->committed = wheel->partial = 0;

	if (distance_squared > MAX_SEGMENT_DISTANCE_SQUARED)
		return; /* Original keeps the old start even here. */

	if (distance_squared > commit_distance_squared ||
		(double)distance_squared > MIN_PARTIAL_DISTANCE_SQUARED) {
		memcpy(segment->end, point, sizeof(point));
		memcpy(segment->normal, contact->normal,
			sizeof(segment->normal));

		if (distance_squared > commit_distance_squared) {
			wheel->committed = 1;

			if (wheel->count == MH_SKID_SEGMENTS)
				wheel->first =
					(wheel->first + 1) % MH_SKID_SEGMENTS;
			else
				wheel->count++;
			wheel->previous = wheel->next;
			wheel->next = (wheel->first + wheel->count) %
				MH_SKID_SEGMENTS;
		} else
			wheel->partial = 1;
	} else
		discard_oldest(wheel);
}

enum mh_error_code
mh_skids_update(const struct mh_skid_input *input, const struct mh_skids *state,
	struct mh_skids *output, struct mh_error *error_out)
{
	struct mh_skids result;
	unsigned int wheel;

	if (input == NULL || state == NULL || output == NULL ||
		!valid_update(input, state))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid skid mark input or history");

	result = *state;

	for (wheel = 0; wheel < 4; wheel++)
		update_wheel(input, wheel, &result.wheels[wheel]);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

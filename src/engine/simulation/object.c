#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/simulation/body_internal.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/object.h"

#define STATE_FLAGS (MH_OBJECT_HIGH_MASS | MH_OBJECT_SLEEPING)
#define SLEEP_TIME_LIMIT 1e9
#define POSITION_LIMIT 1e6
#define SELECTION_DISTANCE 150
#define FALL_SLEEP_HEIGHT -100
#define CONTACT_SLEEP_TIME 6
#define STEP_LIMIT 1
#define GRAVITY 15.81
#define REDUCED_GRAVITY 7.905
#define GRAVITY_SCALE .87
#define CONTACT_LINEAR_DAMPING .02
#define CONTACT_ANGULAR_DAMPING .3
#define AIR_DAMPING .9

static int
valid_state(const struct mh_object_state *state)
{
	unsigned int point;

	if (state == NULL || state->flags > STATE_FLAGS ||
		(state->frame_impact != 0 && state->frame_impact != 1) ||
		!(state->sleep_time >= 0 &&
			state->sleep_time <= SLEEP_TIME_LIMIT) ||
		!mh_motion_valid_object(&state->motion))
		return 0;

	for (point = 0; point < MH_OBJECT_BODY_POINTS; point++)
		if (state->contacts[point].hit != 0 &&
			state->contacts[point].hit != 1)
			return 0;

	return 1;
}

void
mh_object_begin_frame(struct mh_object_state *state)
{
	if (state != NULL)
		state->frame_impact = 0;
}

enum mh_error_code
mh_object_car_near(const float camera[3], const float position[3],
	int *selected, struct mh_error *error_out)
{
	volatile float delta[3];
	long double distance_squared, distance;
	unsigned int axis;

	if (camera == NULL || position == NULL || selected == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scenery selection arguments");

	for (axis = 0; axis < 3; axis++) {
		if (!(camera[axis] >= -POSITION_LIMIT &&
			camera[axis] <= POSITION_LIMIT) ||
			!(position[axis] >= -POSITION_LIMIT &&
				position[axis] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery selection position");

		delta[axis] =
			(float)((long double)camera[axis] - position[axis]);
	}

	distance_squared = (long double)delta[1] * delta[1] +
		(long double)delta[0] * delta[0];
	distance_squared += (long double)delta[2] * delta[2];
	distance = 0;

	if (distance_squared > 0) {
		/*
		 * C89 lacks sqrtl. As in the camera, refine a binary64 seed
		 * before the original extended comparison, with no intervening
		 * float store.
		 */
		distance = sqrt((double)distance_squared);
		distance = (distance + distance_squared / distance) * .5L;
		distance = (distance + distance_squared / distance) * .5L;
	}

	*selected = distance < SELECTION_DISTANCE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
integrate_motion(struct mh_motion_state *motion, unsigned int contact_count,
	double step_seconds, int reduced_gravity)
{
	double gravity, linear_damping, angular_damping, angles[3];
	double delta_rotation[9], rebased_velocity[3];
	unsigned int i, j;

	gravity =
		-(double)(float)(reduced_gravity ? REDUCED_GRAVITY : GRAVITY) *
		GRAVITY_SCALE * step_seconds;

	linear_damping =
		pow((double)(float)(contact_count ? CONTACT_LINEAR_DAMPING
						: AIR_DAMPING),
			step_seconds);
	angular_damping =
		pow((double)(float)(contact_count ? CONTACT_ANGULAR_DAMPING
						: AIR_DAMPING),
			step_seconds);

	for (i = 0; i < 3; i++) {
		motion->velocity[i] =
			(motion->velocity[i] +
				motion->rotation[i * 3 + 1] * gravity) *
			linear_damping;
		motion->angular_velocity[i] *= angular_damping;
		angles[i] = motion->angular_velocity[i] * step_seconds;
	}

	/* Translate in the old basis, then rotate. */
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			motion->position[i] += motion->rotation[j * 3 + i] *
				motion->velocity[j] * step_seconds;

	mh_rotation_turn(motion->rotation, angles);
	memset(delta_rotation, 0, sizeof(delta_rotation));
	delta_rotation[0] = delta_rotation[4] = delta_rotation[8] = 1;
	mh_rotation_turn(delta_rotation, angles);

	for (i = 0; i < 3; i++) {
		rebased_velocity[i] = 0;

		for (j = 0; j < 3; j++)
			rebased_velocity[i] +=
				delta_rotation[i * 3 + j] * motion->velocity[j];
	}

	memcpy(motion->velocity, rebased_velocity, sizeof(rebased_velocity));
}

enum mh_error_code
mh_object_integrate(const struct mh_object_state *input, double step_seconds,
	struct mh_object_state *output, struct mh_error *error_out)
{
	return mh_object_integrate_gravity(
		input, step_seconds, 0, output, error_out);
}

enum mh_error_code
mh_object_integrate_gravity(const struct mh_object_state *input,
	double step_seconds, int reduced_gravity,
	struct mh_object_state *output, struct mh_error *error_out)
{
	struct mh_object_state next;
	struct mh_motion_state *motion;
	unsigned int point, contact_count;

	if (!valid_state(input) || output == NULL ||
		!(step_seconds >= 0 && step_seconds <= STEP_LIMIT) ||
		(reduced_gravity != 0 && reduced_gravity != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid scenery motion");

	next = *input;
	motion = &next.motion;

	if (!(next.flags & MH_OBJECT_SLEEPING)) {
		if (motion->position[1] < FALL_SLEEP_HEIGHT) {
			next.flags |= MH_OBJECT_SLEEPING;
			next.sleep_time = 0;
		} else {
			contact_count = 0;

			for (point = 0; point < MH_OBJECT_BODY_POINTS; point++)
				contact_count += next.contacts[point].hit != 0;

			next.sleep_time = (double)(float)(input->sleep_time +
				step_seconds);

			if (input->sleep_time + step_seconds >
					CONTACT_SLEEP_TIME &&
				contact_count != 0) {
				next.flags |= MH_OBJECT_SLEEPING;
				next.sleep_time = 0;
			}

			integrate_motion(motion, contact_count, step_seconds,
				reduced_gravity);
		}
	}

	if (!valid_state(&next))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"scenery motion exceeds native bounds");

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_object_ground(const struct mh_object_body *body,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_object_state *current, double step_seconds,
	struct mh_object_state *output, struct mh_error *error_out)
{
	struct mh_object_state next;
	struct mh_body_result result;
	enum mh_error_code status;

	if (body == NULL || col == NULL || entry == NULL || output == NULL ||
		!valid_state(current) || !mh_motion_valid_object(entry) ||
		!(step_seconds >= 0 && step_seconds <= STEP_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery ground response");

	next = *current;

	/*
	 * No main/residual queries or retained-contact publication on later
	 * zero-interval world calls. Free motion/sleep checks still ran.
	 */
	if (step_seconds == 0) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_body_object_frame(body, col, entry, &current->motion,
		step_seconds, (next.flags & MH_OBJECT_SLEEPING) != 0,
		&next.frame_impact, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.motion = result.state;
	memcpy(next.contacts, result.contacts, sizeof(next.contacts));
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

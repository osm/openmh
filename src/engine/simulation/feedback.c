#include <math.h>
#include <string.h>

#include "engine/simulation/feedback.h"

#define UPDATE_INTERVAL .16
#define IMPACT_SPRING_DELAY .08f
#define ROAD_SPEED_LIMIT 25.0f
#define ROAD_BASE_PERIOD .06
#define MICROSECONDS_PER_SECOND 1000000.0
#define DIRECTION_SCALE 1000.0L

#define INITIAL_ROAD_MAGNITUDE 2500
#define INITIAL_ROAD_PERIOD_US 50000
#define INITIAL_SPRING_STRENGTH 4000
#define INITIAL_SPRING_DEADBAND 100
#define TRACK_IMPULSE_MIN 500

static long double
absolute(long double value)
{
	return value < 0 ? -value : value;
}

static int
update_due(float timer)
{
	return timer > UPDATE_INTERVAL || timer < 0;
}

void
mh_feedback_reset(struct mh_feedback_state *state)
{
	memset(state, 0, sizeof(*state));

	/* Original initialized data; other timers start at zero. */
	state->timer[MH_FEEDBACK_SPRING] = IMPACT_SPRING_DELAY;
	state->restart = 1;
	state->impact_magnitude = MH_FEEDBACK_MAX_FORCE;
	state->road_magnitude = INITIAL_ROAD_MAGNITUDE;
	state->road_period = INITIAL_ROAD_PERIOD_US;
	state->spring_strength = INITIAL_SPRING_STRENGTH;
	state->spring_deadband = INITIAL_SPRING_DEADBAND;
}

static void
request_update(const struct mh_feedback_state *state,
	const struct mh_feedback_input *input, enum mh_feedback_effect effect,
	struct mh_feedback_update *output)
{
	const struct mh_feedback_status *status;
	int playing;

	status = &input->effects[effect];
	playing = status->playing;

	if (effect == MH_FEEDBACK_IMPACT)
		playing = playing && status->valid;
	else
		playing = playing || status->emulated;

	output->changed[effect] = 1;
	output->query[effect] = !state->restart;
	output->start[effect] = state->restart || !playing;
}

static float
track_impact(const struct mh_feedback_input *input,
	struct mh_feedback_update *output)
{
	const float *normal;
	float impulse, strength;
	long double shape;

	normal = input->track_normal;
	impulse = (float)absolute(input->track_impulse);

	if (impulse < TRACK_IMPULSE_MIN)
		impulse = TRACK_IMPULSE_MIN;

	strength = (float)(absolute(input->forward) * .00007 * impulse);
	output->direction[0] = (long)(-normal[0] * DIRECTION_SCALE);
	output->direction[1] = (long)(normal[2] * DIRECTION_SCALE);
	shape = 1.5 - absolute(normal[0]) * .2;
	shape -= absolute(normal[1]) * .4;
	shape += absolute(normal[2]) * 3.0;

	return (float)(shape * strength);
}

static float
pair_impact(const struct mh_feedback_input *input,
	struct mh_feedback_update *output)
{
	const float *vector;
	long double squared, length, scale, x, z;

	vector = input->pair_impulse;
	squared = (long double)vector[1] * vector[1] +
		(long double)vector[0] * vector[0] +
		(long double)vector[2] * vector[2];

	if (squared == 0)
		return 0;

	/* C89 sqrt seed, refined before the original binary32 stores. */
	length = sqrt((double)squared);
	length = (length + squared / length) * .5L;
	length = (length + squared / length) * .5L;
	scale = DIRECTION_SCALE / length;
	x = vector[0] * scale;
	z = vector[2] * scale;
	output->pair_impulse[0] = (float)x;
	output->pair_impulse[2] = (float)z;

	/*
	 * The original applies this scale twice to direction, once to globals.
	 */
	output->direction[0] = (long)(-x * scale);
	output->direction[1] = (long)(z * scale);

	return (float)((absolute(input->forward) * .03 + 3.0 + length) * .15);
}

static void
impact_step(struct mh_feedback_state *state,
	const struct mh_feedback_input *input,
	struct mh_feedback_update *output)
{
	float strength;

	if (!input->effects[MH_FEEDBACK_IMPACT].available ||
		!update_due(state->timer[MH_FEEDBACK_IMPACT]))
		return;

	if (!input->track_hit && !input->pair_hit)
		return;

	if (input->track_hit)
		strength = track_impact(input, output);
	else
		strength = pair_impact(input, output);

	if (strength > 1)
		strength = 1;

	state->impact_magnitude =
		(long)((long double)strength * MH_FEEDBACK_MAX_FORCE);
	state->timer[MH_FEEDBACK_IMPACT] = 0;
	state->timer[MH_FEEDBACK_ROAD] = 0;
	state->timer[MH_FEEDBACK_SPRING] = IMPACT_SPRING_DELAY;
	request_update(state, input, MH_FEEDBACK_IMPACT, output);
}

static float
road_amount(const struct mh_feedback_input *input, float speed)
{
	float surface, amount;
	long double value;

	surface = 0;

	if (input->front_contact[0] && input->front_surface[0])
		surface = (float)((long double)input->front_bumpy[0] + .15);

	if (input->front_contact[1] && input->front_surface[1] &&
		(long double)surface + (-.15) < input->front_bumpy[1])
		surface = (float)((long double)input->front_bumpy[1] + .15);

	if (input->sliding && surface < .2) {
		if (input->wheelspin)
			value = (long double)input->slide * 20.0 +
				((long double)speed * .5 + 10.0);
		else
			value = (long double)input->slide * 6.0 + speed;
	} else
		value = (long double)speed * 1.5;

	amount = (float)(value * surface);

	if (amount < .15)
		amount = .15f;

	return amount;
}

static void
road_step(struct mh_feedback_state *state,
	const struct mh_feedback_input *input,
	struct mh_feedback_update *output)
{
	float speed, amount;
	unsigned long period, magnitude;

	if (!input->effects[MH_FEEDBACK_ROAD].available ||
		!update_due(state->timer[MH_FEEDBACK_ROAD]))
		return;

	state->timer[MH_FEEDBACK_ROAD] = 0;
	speed = (float)absolute(input->forward);

	if (speed > ROAD_SPEED_LIMIT)
		speed = ROAD_SPEED_LIMIT;

	amount = road_amount(input, speed);
	period = (unsigned long)((ROAD_BASE_PERIOD -
					(long double)speed * .0001) *
		MICROSECONDS_PER_SECOND);
	magnitude = (unsigned long)(((long double)amount * .03 + .1) *
		MH_FEEDBACK_MAX_FORCE);

	if (magnitude > MH_FEEDBACK_MAX_FORCE)
		magnitude = MH_FEEDBACK_MAX_FORCE;

	if (period == state->road_period && magnitude == state->road_magnitude)
		return;

	state->road_period = period;
	state->road_magnitude = magnitude;
	request_update(state, input, MH_FEEDBACK_ROAD, output);
}

static void
spring_step(struct mh_feedback_state *state,
	const struct mh_feedback_input *input,
	struct mh_feedback_update *output)
{
	float strength;
	long coefficient;

	if (!input->effects[MH_FEEDBACK_SPRING].available ||
		!update_due(state->timer[MH_FEEDBACK_SPRING]))
		return;

	state->timer[MH_FEEDBACK_SPRING] = 0;
	strength = .3f;

	if (input->front_contact[0] || input->front_contact[1]) {
		strength = .5f;

		if (!input->sliding)
			strength = (float)(absolute(input->lateral) * .05 + .5);
	}

	if (strength > 1)
		strength = 1;

	coefficient = (long)((long double)strength * MH_FEEDBACK_MAX_FORCE);

	if (coefficient != state->spring_strength) {
		state->spring_strength = coefficient;
		state->spring_deadband = 0;
		request_update(state, input, MH_FEEDBACK_SPRING, output);
	}

	/*
	 * Clears even when unchanged, but only after a due, available spring.
	 */
	state->restart = 0;
}

void
mh_feedback_step(struct mh_feedback_state *state,
	const struct mh_feedback_input *input, float elapsed,
	struct mh_feedback_update *output)
{
	unsigned int effect;

	memset(output, 0, sizeof(*output));
	memcpy(output->pair_impulse, input->pair_impulse,
		sizeof(output->pair_impulse));

	if (!input->enabled)
		return;

	for (effect = 0; effect < MH_FEEDBACK_EFFECTS; effect++)
		state->timer[effect] =
			(float)((long double)state->timer[effect] + elapsed);

	impact_step(state, input, output);
	road_step(state, input, output);
	spring_step(state, input, output);
}

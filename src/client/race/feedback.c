#include <string.h>

#include "client/race/internal.h"
#include "engine/game/mode.h"
#include "interfaces/input/force.h"

#define IMPACT_DURATION_MS 150
#define INITIAL_DIRECTION 100
#define SECOND_SPRING_STRENGTH 2500
#define SECOND_SPRING_DEADBAND 100

static void
effect_parameters(const struct mh_feedback_state *state,
	const struct mh_feedback_update *update,
	struct mh_force_effect effects[MH_FORCE_EFFECTS])
{
	struct mh_force_effect *effect;

	memset(effects, 0, sizeof(*effects) * MH_FORCE_EFFECTS);
	effect = &effects[MH_FORCE_CONSTANT];
	effect->duration_ms = IMPACT_DURATION_MS;
	effect->magnitude = state->impact_magnitude;
	effect->direction[0] = update == NULL ? 0 : update->direction[0];
	effect->direction[1] =
		update == NULL ? INITIAL_DIRECTION : update->direction[1];
	effect = &effects[MH_FORCE_TRIANGLE];
	effect->duration_ms = MH_FORCE_INFINITE;
	effect->magnitude = (long)state->road_magnitude;
	effect->period_us = state->road_period;
	effect->direction[0] = INITIAL_DIRECTION;
	effect = &effects[MH_FORCE_SPRING];
	effect->duration_ms = MH_FORCE_INFINITE;
	effect->coefficient[0] = state->spring_strength;
	effect->saturation[0] = (unsigned long)state->spring_strength;
	effect->deadband[0] = (unsigned long)state->spring_deadband;
	effect->coefficient[1] = SECOND_SPRING_STRENGTH;
	effect->saturation[1] = SECOND_SPRING_STRENGTH;
	effect->deadband[1] = SECOND_SPRING_DEADBAND;
}

void
mh_race_feedback_stop(struct mh_race_loop *loop)
{
	if (loop->feedback_active)
		mh_input_force_stop(&loop->devices);

	loop->feedback_active = 0;
	loop->feedback_failed = 0;
}

static int
feedback_enabled(const struct mh_race_loop *loop)
{
	return loop->context->controls != NULL &&
		loop->context->controls->force_feedback && loop->focused &&
		!loop->paused && !loop->menu.visible && !loop->screen.visible &&
		loop->capture == NULL && !loop->field->attract &&
		loop->field->playback != MH_GAME_DEMO && !loop->drive->remote &&
		!(loop->session != NULL && loop->session->benchmark);
}

void
mh_race_feedback_frame(struct mh_race_loop *loop)
{
	struct mh_feedback_state initial, *state;
	struct mh_feedback_input input;
	struct mh_feedback_update update;
	struct mh_force_effect effects[MH_FORCE_EFFECTS];
	struct mh_force_status device;
	struct mh_error error;
	float timers[MH_FEEDBACK_EFFECTS];
	unsigned int i, changed, start, available;
	enum mh_error_code status;

	if (!feedback_enabled(loop)) {
		mh_race_feedback_stop(loop);
		return;
	}

	if (loop->feedback_failed)
		return;

	state = &loop->history->feedback;

	if (!loop->history->feedback_initialized) {
		mh_feedback_reset(state);
		loop->history->feedback_initialized = 1;
	}

	mh_feedback_reset(&initial);
	effect_parameters(&initial, NULL, effects);
	status = mh_input_force_poll(&loop->devices, effects, &device, &error);

	if (status != MH_ERROR_NONE)
		goto failed;

	available = 0;

	for (i = 0; i < MH_FORCE_EFFECTS; i++)
		available += device.available[i] != 0;

	if (!loop->feedback_active ||
		loop->feedback_generation != device.generation) {
		memcpy(timers, state->timer, sizeof(timers));
		*state = initial;
		memcpy(state->timer, timers, sizeof(timers));
		loop->feedback_generation = device.generation;
		printf("Force feedback: impact=%s road=%s spring=%s\n",
			device.available[MH_FORCE_CONSTANT] ? "available"
							: "unavailable",
			device.available[MH_FORCE_TRIANGLE] ? "available"
							: "unavailable",
			device.available[MH_FORCE_SPRING] ? "available"
							: "unavailable");
	}

	loop->feedback_active = 1;

	if (!available)
		return;

	memset(&input, 0, sizeof(input));
	input.enabled = 1;

	for (i = 0; i < MH_FEEDBACK_EFFECTS; i++) {
		input.effects[i].available = device.available[i];
		input.effects[i].playing = device.playing[i];
		input.effects[i].valid = 1;
	}

	mh_feedback_sample(loop->field, &input);
	mh_feedback_step(state, &input, loop->control_seconds, &update);
	memcpy(loop->field->feedback_pair, update.pair_impulse,
		sizeof(loop->field->feedback_pair));
	effect_parameters(state, &update, effects);
	changed = start = 0;

	for (i = 0; i < MH_FEEDBACK_EFFECTS; i++) {
		if (update.changed[i])
			changed |= 1U << i;

		if (update.start[i])
			start |= 1U << i;
	}

	status = mh_input_force_apply(
		&loop->devices, effects, changed, start, &error);

	if (status == MH_ERROR_NONE)
		return;

failed:
	fprintf(stderr, "Force feedback: %s\n", error.message);
	mh_input_force_stop(&loop->devices);
	loop->feedback_active = 0;
	loop->feedback_failed = 1;
}

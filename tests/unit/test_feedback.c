#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/simulation/feedback.h"

static int
test_sequence(void)
{
	struct mh_feedback_state state, saved;
	struct mh_feedback_input input;
	struct mh_feedback_update update;
	unsigned int i;

	mh_feedback_reset(&state);
	memset(&input, 0, sizeof(input));

	if (!state.restart || state.timer[0] != 0 || state.timer[1] != 0 ||
		state.timer[2] != .08f ||
		state.impact_magnitude != MH_FEEDBACK_MAX_FORCE ||
		state.road_magnitude != 2500 || state.road_period != 50000 ||
		state.spring_strength != 4000 || state.spring_deadband != 100)
		return 1;

	for (i = 0; i < MH_FEEDBACK_EFFECTS; i++)
		input.effects[i].available = 1;

	input.enabled = 1;
	input.front_contact[0] = input.front_surface[0] = 1;
	input.forward = 10;
	input.lateral = 2;
	mh_feedback_step(&state, &input, .17f, &update);

	if (update.changed[0] || !update.changed[1] || !update.changed[2] ||
		!update.start[1] || !update.start[2] || state.restart)
		return 1;

	mh_feedback_step(&state, &input, .17f, &update);

	if (update.changed[0] || update.changed[1] || update.changed[2])
		return 1;

	input.track_hit = 1;
	input.track_normal[0] = 1;
	mh_feedback_step(&state, &input, .17f, &update);

	if (!update.changed[0] || update.changed[1] || update.changed[2] ||
		state.timer[0] != 0 || state.timer[1] != 0 ||
		state.timer[2] != .08f)
		return 1;

	saved = state;
	input.enabled = 0;
	mh_feedback_step(&state, &input, 1, &update);

	for (i = 0; i < MH_FEEDBACK_EFFECTS; i++)
		if (state.timer[i] != saved.timer[i] || update.changed[i])
			return 1;

	return 0;
}

int
main(void)
{
	return test_sequence();
}

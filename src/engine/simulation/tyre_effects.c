#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/tyre_effects.h"

#define MATERIAL_ID_LIMIT 25
#define EFFECT_ID_LIMIT 8
#define VELOCITY_LIMIT 1e4
#define SLIDE_LIMIT 1e12
#define RPM_LIMIT 1e6
#define EMISSION_SPEED 3
#define SLIDE_GAIN .5L
#define SPIN_GAIN .000175
#define BRAKE_GAIN .1

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

void
mh_tyre_effects_begin_frame(struct mh_tyre_effects *effects)
{
	if (effects != NULL)
		effects->id[0] = effects->id[1] = 0;
}

enum mh_error_code
mh_tyre_effects_update(const struct mh_materials *materials,
	const struct mh_tyre_effect_input *input,
	const struct mh_tyre_effects *state, struct mh_tyre_effects *output,
	struct mh_error *error_out)
{
	struct mh_tyre_effects result;
	unsigned int wheel;
	unsigned long material_id, effect_id;
	long double gain;
	int emit;
	float forward;

	if (materials == NULL || input == NULL || state == NULL ||
		output == NULL || materials->count == 0 ||
		materials->count > MH_MATERIAL_LIMIT ||
		!valid_flag(input->sliding) || !valid_flag(input->wheelspin) ||
		!valid_flag(input->heavy_brake) ||
		!(input->forward >= -VELOCITY_LIMIT &&
			input->forward <= VELOCITY_LIMIT) ||
		!(input->slide >= 0 && input->slide <= SLIDE_LIMIT) ||
		!(input->rpm >= 0 && input->rpm <= RPM_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid tyre effect input");

	result = *state;
	forward = (float)input->forward;

	for (wheel = 0; wheel < 2; wheel++) {
		if (!valid_flag(input->supported[wheel]) ||
			state->id[wheel] > EFFECT_ID_LIMIT ||
			!(state->gain[wheel] >= 0 && state->gain[wheel] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid tyre effect state");

		if (!input->supported[wheel])
			continue;

		material_id = input->material[wheel] > MATERIAL_ID_LIMIT
			? 0
			: input->material[wheel];

		if (material_id >= materials->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid tyre effect material");

		effect_id = materials->entries[material_id].contact_anim;

		if (effect_id < 1 || effect_id > EFFECT_ID_LIMIT)
			continue;

		emit = 0;
		gain = 0;

		/* Later gates override earlier gains, including zero gains. */
		if (input->sliding && forward > EMISSION_SPEED) {
			emit = 1;
			gain = (long double)(float)input->slide * SLIDE_GAIN;
		}

		if (input->wheelspin) {
			emit = 1;
			gain = (long double)(float)input->rpm *
				(long double)SPIN_GAIN;
		}

		if (input->heavy_brake && forward > EMISSION_SPEED) {
			emit = 1;
			gain = (long double)forward * (long double)BRAKE_GAIN;
		}

		if (emit) {
			result.id[wheel] = effect_id;
			result.gain[wheel] = gain > 1 ? 1 : (double)(float)gain;
		}
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

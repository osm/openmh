#include <stdio.h>
#include <string.h>

/* Identical input cases qualify each profile through its own entry points. */
#ifdef MH_TEST_A3D12
#include "engine/audio/a3d12_source.h"
#define SOURCE_PREPARE mh_a3d12_source_prepare
#define SOURCE_SLOT mh_a3d12_source_slot
#define VOICE_COUNT MH_A3D12_VOICES
#else
#include "engine/audio/ds3d_source.h"
#define SOURCE_PREPARE mh_ds3d_source_prepare
#define SOURCE_SLOT mh_ds3d_source_slot
#define VOICE_COUNT MH_DS3D_VOICES
#endif
#include "engine/core/binary.h"

static int
check_sequence(void)
{
	struct mh_spatial_source_state state, voice_states[VOICE_COUNT];
	const struct mh_spatial_source_state *voices[VOICE_COUNT];
	struct mh_spatial_source_input input;
	float anchor[3];
	struct mh_spatial_source_parameters parameters;
	struct mh_spatial_source_update output;
	unsigned int voice_index;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(anchor, 0, sizeof(anchor));
	memset(&parameters, 0, sizeof(parameters));
	input.spatial = input.has_buffer = 1;
	input.elapsed = .125f;
	input.sample_rate = 22050;
	input.pitch = 2;
	input.near_distance = 5;
	input.far_distance = 40;
	input.position[0] = 25;
	input.priority = 2;
	parameters.gain = .5f;
	parameters.pan = .25f;

	if (!SOURCE_PREPARE(
		&state, &input, anchor, 1, .5f, &parameters, &output) ||
		parameters.gain != .25f || parameters.pan != .25f ||
		parameters.frequency != 44100 || state.score != 2.25f ||
		!output.changed || output.velocity[0] != 200)
		return 1;

	input.position[0] = 50;

	if (SOURCE_PREPARE(
		&state, &input, anchor, 1, 1, &parameters, &output) ||
		state.position[0] != 50 || state.score != 2.25f ||
		parameters.gain != .25f || output.velocity[0] != 200)
		return 1;

	input.position[0] = 25;

	if (!SOURCE_PREPARE(
		&state, &input, anchor, 1, 1, &parameters, &output) ||
		output.velocity[0] != -200)
		return 1;

	/* Missing bindings must not consume source motion or scratch. */
	input.position[0] = 30;
	parameters.frequency = 7;

	if (SOURCE_PREPARE(
		&state, &input, anchor, 0, 1, &parameters, &output) ||
		state.position[0] != 25 || parameters.frequency != 7 ||
		output.changed)
		return 1;

	input.spatial = 0;
	input.gain = 0;
	input.pan = -.5f;

	if (!SOURCE_PREPARE(&state, &input, NULL, 0, 1, &parameters, &output) ||
		parameters.gain != 0 || parameters.pan != -.5f ||
		state.position[0] != 25 || output.changed)
		return 1;

	for (voice_index = 0; voice_index < VOICE_COUNT; voice_index++) {
		voice_states[voice_index].score = 2;
		voices[voice_index] = &voice_states[voice_index];
	}

	if (SOURCE_SLOT(2, voices) != 0 || SOURCE_SLOT(3, voices) != 1)
		return 1;

	voices[7] = NULL;

	if (SOURCE_SLOT(0, voices) != 8)
		return 1;

	puts("Bound source range, retained motion, binding gates and zero gain and voice choices passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}

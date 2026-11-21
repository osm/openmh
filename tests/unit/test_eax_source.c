#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_source.h"
#include "engine/core/binary.h"

static int
check_sequence(void)
{
	struct mh_spatial_source_state state, voice_states[MH_EAX_VOICES];
	const struct mh_spatial_source_state *voices[MH_EAX_VOICES];
	struct mh_spatial_source_input input;
	struct mh_eax_source_listener listener;
	struct mh_spatial_source_parameters parameters;
	struct mh_spatial_source_update output;
	unsigned int voice_index;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&listener, 0, sizeof(listener));
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

	if (!mh_eax_source_prepare(
		&state, &input, &listener, .5f, &parameters, &output) ||
		parameters.gain != .25f || parameters.pan != .25f ||
		parameters.frequency != 44100 || state.score != 2.25f ||
		!output.changed || output.velocity[0] != 200)
		return 1;

	input.position[0] = 50;

	if (mh_eax_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		state.position[0] != 50 || state.score != 2.25f ||
		parameters.gain != .25f || output.velocity[0] != 200)
		return 1;

	input.position[0] = 25;

	if (!mh_eax_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		output.velocity[0] != -200)
		return 1;

	input.spatial = 0;
	input.gain = 1;
	input.pan = -.5f;
	listener.right[2] = 1;
	listener.position[0] = 10;
	listener.previous[0] = 8;

	if (!mh_eax_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		output.position[0] != 10 || output.position[2] != -.5f ||
		output.velocity[0] != 16 || state.position[0] != 25)
		return 1;

	for (voice_index = 0; voice_index < MH_EAX_VOICES; voice_index++) {
		voice_states[voice_index].score = 2;
		voices[voice_index] = &voice_states[voice_index];
	}

	if (mh_eax_source_slot(2, voices) != 0 ||
		mh_eax_source_slot(3, voices) != 1)
		return 1;

	voices[7] = NULL;

	if (mh_eax_source_slot(0, voices) != 8)
		return 1;

	puts("EAX source range, retained motion, relative positioning and voice choices passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}

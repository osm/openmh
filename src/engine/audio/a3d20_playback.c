#include <stddef.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/a3d20_playback.h"
#include "engine/audio/spatial_levels.h"
#include "engine/core/text.h"

#define SPATIAL_GAIN 3.0
#define NONSPATIAL_GAIN 1.5

struct sample_scale {
	const char	*name;
	double		scale;
};

static float
sample_gain(const char *name, float gain)
{
	static const struct sample_scale samples[] = {
		{MH_ASSET_SAMPLE_GASON, 1.6}, {MH_ASSET_SAMPLE_TURBO, .5},
		{MH_ASSET_SAMPLE_RULLGRU, 2}, {MH_ASSET_SAMPLE_GEAR3, .9},
		{MH_ASSET_SAMPLE_ASFALT2, .7}};

	size_t sample_index;

	for (sample_index = 0;
		sample_index < sizeof(samples) / sizeof(samples[0]);
		sample_index++)
		if (mh_ascii_equal(name, samples[sample_index].name))
			return (float)((long double)gain *
				samples[sample_index].scale);

	return gain;
}

static void
playback_levels(const struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_a3d20_playback_request *output)
{
	float gain, frequency, attack, release;
	long double level;

	gain = input->spatial ? input->gain : input->prepared_gain;
	gain = sample_gain(input->name, gain);
	gain = (float)((long double)gain *
		(input->spatial ? SPATIAL_GAIN : NONSPATIAL_GAIN));
	frequency = (float)((long double)input->sample_rate * input->pitch);
	attack = mh_spatial_fade_in(
		state->started, input->fade_in_samples, frequency, clock);
	release = output->action == MH_A3D20_PLAYBACK_START
		? 1
		: mh_spatial_fade_out(state->fading, input->fade_out_samples,
			frequency, clock);
	level = (long double)release * attack * gain;
	output->gain = level > 1 ? 1 : (float)level;
	output->pitch = input->pitch;
}

void
mh_a3d20_playback_start(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_a3d20_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_source)
		return;

	state->started = mh_spatial_timestamp(clock);
	state->fading = 0;
	state->skip_update = 1;
	output->action = MH_A3D20_PLAYBACK_START;
	output->loop = input->loop;
	playback_levels(state, input, clock, output);
}

void
mh_a3d20_playback_stop(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	struct mh_a3d20_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_source)
		return;

	state->active = state->skip_update = 0;
	state->started = state->fading = 0;
	output->action = MH_A3D20_PLAYBACK_STOP;
}

void
mh_a3d20_playback_step(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	const struct mh_a3d20_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_a3d20_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_source)
		return;

	if (state->skip_update) {
		state->skip_update = 0;
		return;
	}

	if (!status->playing || !status->eligible ||
		mh_spatial_release_finished(&state->fading, input->loop,
			status->looping, input->fade_out_samples, input->pitch,
			clock)) {
		if (state->active)
			mh_a3d20_playback_stop(state, input, output);

		return;
	}

	output->action = MH_A3D20_PLAYBACK_UPDATE;
	playback_levels(state, input, clock, output);
}

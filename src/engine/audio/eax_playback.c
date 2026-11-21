#include <stddef.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/eax_playback.h"
#include "engine/audio/spatial_levels.h"
#include "engine/core/text.h"

struct sample_scale {
	const char	*name;
	double		scale;
};

static float
sample_gain(const char *name, float gain)
{
	static const struct sample_scale samples[] = {
		{MH_ASSET_SAMPLE_GASON, 1.9}, {MH_ASSET_SAMPLE_GASRELE, .5},
		{MH_ASSET_SAMPLE_TURBO, .55}, {MH_ASSET_SAMPLE_RULLGRU, 1.5},
		{MH_ASSET_SAMPLE_GEAR3, 8}, {MH_ASSET_SAMPLE_ASFALT2, .03}};

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
playback_levels(const struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_eax_playback_request *output)
{
	float frequency, gain, attack, release;

	frequency = mh_spatial_frequency(input->frequency);
	output->frequency = (unsigned long)frequency;
	gain = sample_gain(input->name, input->gain);
	attack = mh_spatial_fade_in(
		state->started, input->fade_in_samples, frequency, clock);
	release = output->action == MH_EAX_PLAYBACK_START
		? 1
		: mh_spatial_fade_out(state->fading, input->fade_out_samples,
			frequency, clock);
	output->volume =
		mh_spatial_volume(gain, attack, release, &output->log_gain);
}

void
mh_eax_playback_start(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_eax_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	state->started = mh_spatial_timestamp(clock);
	state->fading = 0;
	output->action = MH_EAX_PLAYBACK_START;
	output->loop = input->loop;
	playback_levels(state, input, clock, output);
}

void
mh_eax_playback_stop(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	struct mh_eax_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	state->active = 0;
	state->started = state->fading = 0;
	output->action = MH_EAX_PLAYBACK_STOP;
}

void
mh_eax_playback_step(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	const struct mh_eax_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_eax_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	if (!status->playing || !status->eligible ||
		mh_spatial_release_finished(&state->fading, input->loop,
			status->looping, input->fade_out_samples,
			input->frequency, clock)) {
		if (state->active)
			mh_eax_playback_stop(state, input, output);

		return;
	}

	output->action = MH_EAX_PLAYBACK_UPDATE;
	playback_levels(state, input, clock, output);
}

#include <math.h>
#include <string.h>

#include "engine/audio/spatial_levels.h"
#include "engine/audio/spatial_playback.h"

#define NONSPATIAL_GAIN .6
#define PAN_GAIN 10.0f
#define PAN_LIMIT 10000.0f

static void
playback_pan(float pan, struct mh_spatial_playback_request *output)
{
	long double logarithm;
	float value;
	int sign;

	sign = pan < 0 ? -1 : 1;
	logarithm =
		sign * log((double)(1 + (long double)pan * PAN_GAIN * sign));
	output->log_pan = (float)logarithm;
	value = (float)(logarithm * PAN_LIMIT);

	if (value > PAN_LIMIT)
		value = PAN_LIMIT;

	if (value < -PAN_LIMIT)
		value = -PAN_LIMIT;

	output->pan = (long)value;
	output->set_pan = 1;
}

static void
playback_levels(const struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output)
{
	float frequency, gain, attack, release;

	frequency = mh_spatial_frequency(input->frequency);
	output->frequency = (unsigned long)frequency;
	gain = input->spatial
		? input->gain
		: (float)((long double)input->prepared_gain * NONSPATIAL_GAIN);
	attack = mh_spatial_fade_in(
		state->started, input->fade_in_samples, frequency, clock);
	release = output->action == MH_SPATIAL_PLAYBACK_START
		? 1
		: mh_spatial_fade_out(state->fading, input->fade_out_samples,
			frequency, clock);
	output->volume =
		mh_spatial_volume(gain, attack, release, &output->log_gain);
}

void
mh_spatial_playback_start(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	if (!input->spatial)
		playback_pan(input->pan, output);

	state->started = mh_spatial_timestamp(clock);
	state->fading = 0;
	output->action = MH_SPATIAL_PLAYBACK_START;
	output->loop = input->loop;
	playback_levels(state, input, clock, output);
}

void
mh_spatial_playback_stop(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	struct mh_spatial_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	state->active = 0;
	state->started = state->fading = 0;
	output->action = MH_SPATIAL_PLAYBACK_STOP;
}

void
mh_spatial_playback_step(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_spatial_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output)
{
	memset(output, 0, sizeof(*output));

	if (!input->loaded || !input->has_buffer)
		return;

	if (!status->playing || !status->eligible ||
		mh_spatial_release_finished(&state->fading, input->loop,
			status->looping, input->fade_out_samples,
			input->frequency, clock)) {
		if (state->active)
			mh_spatial_playback_stop(state, input, output);

		return;
	}

	output->action = MH_SPATIAL_PLAYBACK_UPDATE;

	if (!input->spatial)
		playback_pan(input->pan, output);

	playback_levels(state, input, clock, output);
}

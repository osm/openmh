#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/audio/mixer.h"
#include "engine/audio/reverb.h"

#define CHANNELS 2
#define COMBS 4
#define DIFFUSERS 2
#define LINES (COMBS + DIFFUSERS)
#define DIFFUSION_GAIN .5
#define STEREO_DELAY .0007
#define VOLUME_RAMP_SECONDS .005
#define DECAY_MIN_SECONDS .1
#define DECAY_MAX_SECONDS 20
#define DECAY_RESIDUAL .001
#define TAIL_FLOOR 1e-20

struct delay_line {
	float	*samples;
	size_t	length, cursor;
	double	feedback, filtered;
};

struct mh_reverb {
	struct delay_line		lines[CHANNELS][LINES];
	float				*storage;
	size_t				sample_count;
	size_t				ramp_frames, ramp_remaining;
	unsigned long			rate;
	struct mh_reverb_settings	settings;
	double				volume, volume_step;
};

static int
settings_valid(const struct mh_reverb_settings *settings)
{
	return settings != NULL && settings->volume >= 0 &&
		settings->volume <= 1 && settings->decay >= DECAY_MIN_SECONDS &&
		settings->decay <= DECAY_MAX_SECONDS &&
		settings->damping >= 0 && settings->damping <= 1;
}

static void
advance_delay(struct delay_line *line)
{
	line->cursor++;

	if (line->cursor == line->length)
		line->cursor = 0;
}

static double
comb_step(struct delay_line *line, double input, double damping)
{
	double delayed;

	delayed = line->samples[line->cursor];
	line->filtered = delayed * (1 - damping) + line->filtered * damping;

	if (fabs(line->filtered) < TAIL_FLOOR)
		line->filtered = 0;

	line->samples[line->cursor] =
		(float)(input + line->filtered * line->feedback);
	advance_delay(line);

	return delayed;
}

static double
diffuse_step(struct delay_line *line, double input)
{
	double output;

	output = line->samples[line->cursor] - DIFFUSION_GAIN * input;
	line->samples[line->cursor] = (float)(input + DIFFUSION_GAIN * output);

	if (fabs(line->samples[line->cursor]) < TAIL_FLOOR)
		line->samples[line->cursor] = 0;

	advance_delay(line);

	return output;
}

static double
channel_step(struct mh_reverb *reverb, unsigned int channel, double input)
{
	double output;
	unsigned int line_index;

	output = 0;

	for (line_index = 0; line_index < COMBS; line_index++)
		output += comb_step(&reverb->lines[channel][line_index], input,
			reverb->settings.damping);

	output /= COMBS;

	for (line_index = COMBS; line_index < LINES; line_index++)
		output = diffuse_step(
			&reverb->lines[channel][line_index], output);

	return output;
}

static void
set_parameters(
	struct mh_reverb *reverb, const struct mh_reverb_settings *settings)
{
	struct delay_line *line;
	unsigned int channel, line_index;

	for (channel = 0; channel < CHANNELS; channel++)
		for (line_index = 0; line_index < COMBS; line_index++) {
			line = &reverb->lines[channel][line_index];
			line->feedback = pow(DECAY_RESIDUAL,
				(double)line->length / reverb->rate /
					settings->decay);
		}

	if (settings->volume != reverb->settings.volume) {
		reverb->ramp_remaining = reverb->ramp_frames;
		reverb->volume_step = (settings->volume - reverb->volume) /
			reverb->ramp_remaining;
	}

	reverb->settings = *settings;
}

static enum mh_error_code
allocate_delays(struct mh_reverb *reverb, struct mh_error *error_out)
{
	/* Native tuning; these delay times are not EAX constants. */
	static const double delay_seconds[LINES] = {
		.0297, .0371, .0411, .0437, .005, .0017};

	struct delay_line *line;
	unsigned int channel, line_index;
	size_t offset;

	for (channel = 0; channel < CHANNELS; channel++)
		for (line_index = 0; line_index < LINES; line_index++) {
			line = &reverb->lines[channel][line_index];
			line->length =
				(size_t)((delay_seconds[line_index] +
						channel * STEREO_DELAY) *
						reverb->rate +
					.5);
			reverb->sample_count += line->length;
		}

	/* Bounded rate and fixed times keep this allocation below 256 KiB. */
	reverb->storage =
		calloc(reverb->sample_count, sizeof(*reverb->storage));

	if (reverb->storage == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate reverb delays");

	offset = 0;

	for (channel = 0; channel < CHANNELS; channel++)
		for (line_index = 0; line_index < LINES; line_index++) {
			line = &reverb->lines[channel][line_index];
			line->samples = reverb->storage + offset;
			offset += line->length;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_reverb_create(unsigned long rate, const struct mh_reverb_settings *settings,
	struct mh_reverb **output, struct mh_error *error_out)
{
	struct mh_reverb *reverb;
	enum mh_error_code status;

	if (output == NULL || rate < MH_MIXER_RATE_MIN ||
		rate > MH_MIXER_RATE_MAX || !settings_valid(settings))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid reverb initialization");

	reverb = calloc(1, sizeof(*reverb));

	if (reverb == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate reverb state");

	reverb->rate = rate;
	reverb->ramp_frames = (size_t)(rate * VOLUME_RAMP_SECONDS);
	status = allocate_delays(reverb, error_out);

	if (status != MH_ERROR_NONE) {
		mh_reverb_destroy(reverb);
		return status;
	}

	set_parameters(reverb, settings);
	mh_reverb_clear(reverb);
	*output = reverb;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_reverb_destroy(struct mh_reverb *reverb)
{
	if (reverb == NULL)
		return;

	free(reverb->storage);
	free(reverb);
}

enum mh_error_code
mh_reverb_configure(struct mh_reverb *reverb,
	const struct mh_reverb_settings *settings, struct mh_error *error_out)
{
	if (reverb == NULL || !settings_valid(settings))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid reverb settings");

	set_parameters(reverb, settings);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_reverb_clear(struct mh_reverb *reverb)
{
	struct delay_line *line;
	unsigned int channel, line_index;

	if (reverb == NULL)
		return;

	memset(reverb->storage, 0,
		reverb->sample_count * sizeof(*reverb->storage));

	for (channel = 0; channel < CHANNELS; channel++)
		for (line_index = 0; line_index < LINES; line_index++) {
			line = &reverb->lines[channel][line_index];
			line->cursor = 0;
			line->filtered = 0;
		}

	reverb->ramp_remaining = 0;
	reverb->volume = reverb->settings.volume;
}

enum mh_error_code
mh_reverb_process(struct mh_reverb *reverb, const float *send, float *stereo,
	size_t frames, int paused, struct mh_error *error_out)
{
	size_t frame;
	double input, left, right;

	if (reverb == NULL || send == NULL || stereo == NULL ||
		frames > MH_MIXER_FRAMES_MAX || (paused != 0 && paused != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid reverb render");

	if (paused) {
		memset(stereo, 0, frames * CHANNELS * sizeof(*stereo));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (frame = 0; frame < frames; frame++) {
		input = ((double)send[frame * CHANNELS] +
				send[frame * CHANNELS + 1]) *
			.5;
		left = channel_step(reverb, 0, input);
		right = channel_step(reverb, 1, input);

		if (reverb->ramp_remaining != 0) {
			reverb->volume += reverb->volume_step;
			reverb->ramp_remaining--;

			if (reverb->ramp_remaining == 0)
				reverb->volume = reverb->settings.volume;
		}

		if (reverb->volume != 0) {
			stereo[frame * CHANNELS] +=
				(float)(left * reverb->volume);
			stereo[frame * CHANNELS + 1] +=
				(float)(right * reverb->volume);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

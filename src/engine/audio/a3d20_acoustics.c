#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/audio/a3d20_acoustics.h"
#include "engine/audio/mixer_internal.h"

/* Native shelving approximation; not driver filter coefficients. */
#define FILTER_HZ 1000.0
#define TWO_PI 6.2831853071795864769
#define TRANSITION_SECONDS .005

struct acoustic_voice {
	struct mh_a3d20_paths	paths;
	double			low, transmission, high_frequency;
	size_t			transition_remaining;
	int			configured;
};

struct mh_a3d20_acoustics {
	struct acoustic_voice	voices[MH_MIXER_VOICES];
	float			*delay;
	size_t			delay_frames, delay_cursor;
	size_t			transition_frames;
	unsigned long		rate;
	double			filter;
};

void
mh_a3d20_acoustics_restart(
	struct mh_a3d20_acoustics *acoustics, unsigned int slot)
{
	struct acoustic_voice *voice;

	voice = &acoustics->voices[slot];
	memset(voice, 0, sizeof(*voice));
	voice->transmission = voice->high_frequency = 1;
	voice->paths.transmission = voice->paths.transmission_hf = 1;
}

void
mh_a3d20_acoustics_clear(struct mh_a3d20_acoustics *acoustics)
{
	unsigned int slot;

	if (acoustics == NULL)
		return;

	memset(acoustics->delay, 0,
		acoustics->delay_frames * 2 * sizeof(float));
	acoustics->delay_cursor = 0;

	for (slot = 0; slot < MH_MIXER_VOICES; slot++)
		mh_a3d20_acoustics_restart(acoustics, slot);
}

enum mh_error_code
mh_a3d20_acoustics_create(unsigned long rate,
	struct mh_a3d20_acoustics **output, struct mh_error *error_out)
{
	struct mh_a3d20_acoustics *acoustics;

	if (output == NULL || rate < MH_MIXER_RATE_MIN ||
		rate > MH_MIXER_RATE_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D acoustics rate");

	acoustics = calloc(1, sizeof(*acoustics));

	if (acoustics == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate A3D acoustics");

	acoustics->rate = rate;
	acoustics->delay_frames = (size_t)ceil(rate * MH_A3D20_DELAY_MAX) + 2;
	acoustics->transition_frames = (size_t)(rate * TRANSITION_SECONDS);
	acoustics->filter = 1 - exp(-TWO_PI * FILTER_HZ / rate);
	acoustics->delay = calloc(acoustics->delay_frames * 2, sizeof(float));

	if (acoustics->delay == NULL) {
		free(acoustics);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate A3D reflection delay");
	}

	mh_a3d20_acoustics_clear(acoustics);
	*output = acoustics;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_a3d20_acoustics_destroy(struct mh_a3d20_acoustics *acoustics)
{
	if (acoustics == NULL)
		return;

	free(acoustics->delay);
	free(acoustics);
}

void
mh_a3d20_acoustics_update(struct mh_a3d20_acoustics *acoustics,
	unsigned int slot, const struct mh_a3d20_paths *paths)
{
	struct acoustic_voice *voice;

	voice = &acoustics->voices[slot];

	if (!voice->configured) {
		voice->transmission = paths->transmission;
		voice->high_frequency = paths->transmission_hf;
		voice->configured = 1;
	} else if (voice->paths.transmission != paths->transmission ||
		voice->paths.transmission_hf != paths->transmission_hf)
		voice->transition_remaining = acoustics->transition_frames;

	voice->paths = *paths;
}

static double
filter_sample(double sample, double low, double high_frequency)
{
	/* The documented zero endpoint suppresses the entire path. */
	if (high_frequency == 0)
		return 0;

	return high_frequency == 1 ? sample
				: low + high_frequency * (sample - low);
}

static void
emit_reflection(struct mh_a3d20_acoustics *acoustics,
	const struct mh_a3d20_reflection *path, double sample)
{
	size_t first, second;
	double delay, fraction, gains[2];
	unsigned int channel;

	delay = path->delay * acoustics->rate;
	first = (acoustics->delay_cursor + (size_t)delay) %
		acoustics->delay_frames;
	second = (first + 1) % acoustics->delay_frames;
	fraction = delay - floor(delay);
	gains[0] = sqrt((1 - path->pan) * .5) * path->gain;
	gains[1] = sqrt((1 + path->pan) * .5) * path->gain;

	for (channel = 0; channel < 2; channel++) {
		acoustics->delay[first * 2 + channel] +=
			(float)(sample * gains[channel] * (1 - fraction));
		acoustics->delay[second * 2 + channel] +=
			(float)(sample * gains[channel] * fraction);
	}
}

static void
process_voice(void *context, unsigned int slot,
	const struct mh_mixer_voice *source, const double samples[2],
	double stereo[2])
{
	struct mh_a3d20_acoustics *acoustics;
	struct acoustic_voice *voice;
	double direct, wet, gain;
	unsigned int reflection_index;

	acoustics = context;
	voice = &acoustics->voices[slot];

	if (voice->transition_remaining != 0) {
		voice->transmission +=
			(voice->paths.transmission - voice->transmission) /
			voice->transition_remaining;
		voice->high_frequency +=
			(voice->paths.transmission_hf - voice->high_frequency) /
			voice->transition_remaining;
		voice->transition_remaining--;
	}

	voice->low += acoustics->filter * (samples[0] - voice->low);
	direct = filter_sample(samples[0], voice->low, voice->high_frequency) *
		voice->transmission;
	stereo[0] = direct * source->left;
	stereo[1] = direct * source->right;
	gain = sqrt(
		source->left * source->left + source->right * source->right);
	wet = filter_sample(
		samples[0], voice->low, voice->paths.reflection_hf) *
		gain;

	for (reflection_index = 0; reflection_index < voice->paths.count;
		reflection_index++)
		emit_reflection(acoustics,
			&voice->paths.reflections[reflection_index], wet);
}

static void
process_frame(void *context, double stereo[2])
{
	struct mh_a3d20_acoustics *acoustics;
	unsigned int channel;

	acoustics = context;

	for (channel = 0; channel < 2; channel++) {
		stereo[channel] +=
			acoustics->delay[acoustics->delay_cursor * 2 + channel];
		acoustics->delay[acoustics->delay_cursor * 2 + channel] = 0;
	}

	acoustics->delay_cursor =
		(acoustics->delay_cursor + 1) % acoustics->delay_frames;
}

void
mh_a3d20_acoustics_processor(struct mh_a3d20_acoustics *acoustics,
	struct mh_mixer_processor *processor)
{
	processor->context = acoustics;

	processor->voice = process_voice;
	processor->frame = process_frame;
}

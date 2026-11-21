#include <math.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/pcm.h"
#include "engine/core/error.h"

/* Native fixed-slot counterpart of the effects device's zero-step fallback. */
#define ZERO_FREQUENCY_RATE 22050

static void
mix_voice(struct mh_mixer_voice *state, double step, double output[2],
	unsigned int index, const struct mh_mixer_processor *processor)
{
	const struct mh_audio_pcm *wave;
	size_t current, next, end;
	unsigned int channel;
	double fraction, a, b, source[2], stereo[2];

	wave = state->wave;
	end = state->loop_end > state->loop_start ? state->loop_end
						: wave->frames;
	current = (size_t)state->cursor;
	fraction = state->cursor - current;
	next = current + 1;

	if (next >= end)
		next = state->loop_end > state->loop_start ? state->loop_start
							: current;

	for (channel = 0; channel < 2; channel++) {
		a = mh_audio_pcm_sample(
			wave, current, wave->channels == 1 ? 0 : channel);
		b = mh_audio_pcm_sample(
			wave, next, wave->channels == 1 ? 0 : channel);
		source[channel] = a + (b - a) * fraction;
		stereo[channel] = source[channel] *
			(channel == 0 ? state->left : state->right);
	}

	if (processor != NULL)
		processor->voice(
			processor->context, index, state, source, stereo);

	for (channel = 0; channel < 2; channel++)
		output[channel] += stereo[channel];

	state->cursor += step;

	if (state->cursor >= end) {
		if (state->loop_end > state->loop_start)
			state->cursor = state->loop_start +
				fmod(state->cursor - state->loop_start,
					(double)(state->loop_end -
						state->loop_start));
		else
			state->active = 0;
	}
}

static void
advance_transition(struct mh_mixer_voice *voice)
{
	voice->pitch +=
		(voice->target_pitch - voice->pitch) / voice->transition;
	voice->left += (voice->target_left - voice->left) / voice->transition;
	voice->right +=
		(voice->target_right - voice->right) / voice->transition;
	voice->transition--;

	if (voice->transition == 0) {
		voice->pitch = voice->target_pitch;
		voice->left = voice->target_left;
		voice->right = voice->target_right;
	}
}

static enum mh_error_code
mix_frames(struct mh_mixer *mixer, float *stereo, size_t frames, int paused,
	int add, const struct mh_mixer_processor *processor,
	struct mh_error *error_out)
{
	struct mh_mixer_voice *state;
	size_t frame;
	unsigned int voice, channel;
	double steps[MH_MIXER_VOICES], sample, output[2];
	float frequency;
	enum mh_error_code status;

	if ((processor != NULL &&
		(processor->voice == NULL || processor->frame == NULL)) ||
		mixer == NULL || mixer->pool != NULL || stereo == NULL ||
		frames > MH_MIXER_FRAMES_MAX ||
		mixer->rate < MH_MIXER_RATE_MIN ||
		mixer->rate > MH_MIXER_RATE_MAX ||
		!(mixer->gain >= 0 && mixer->gain <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio render request");

	for (voice = 0; !paused && voice < MH_MIXER_VOICES; voice++)
		if (mixer->voices[voice].active) {
			state = &mixer->voices[voice];

			if (state->wave == NULL)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"missing audio sample");

			status = mh_mixer_frequency(state->pitch,
				state->wave->rate, &frequency, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			steps[voice] = (frequency == 0 ? ZERO_FREQUENCY_RATE
						: (double)frequency) /
				mixer->rate;
		}

	for (frame = 0; frame < frames; frame++) {
		output[0] = output[1] = 0;

		for (voice = 0; !paused && voice < MH_MIXER_VOICES; voice++) {
			state = &mixer->voices[voice];

			if (!state->active)
				continue;

			if (state->transition != 0) {
				advance_transition(state);
				mh_mixer_frequency(state->pitch,
					state->wave->rate, &frequency, NULL);
				steps[voice] =
					(frequency == 0 ? ZERO_FREQUENCY_RATE
							: (double)frequency) /
					mixer->rate;
			}

			mix_voice(
				state, steps[voice], output, voice, processor);
		}

		if (!paused && processor != NULL)
			processor->frame(processor->context, output);

		for (channel = 0; channel < 2; channel++) {
			sample = output[channel] * mixer->gain +
				(add ? stereo[frame * 2 + channel] : 0);
			stereo[frame * 2 + channel] = (float)(sample < -1 ? -1
					: sample > 1			  ? 1
						: sample);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_render(struct mh_mixer *mixer, float *stereo, size_t frames,
	int paused, struct mh_error *error_out)
{
	return mix_frames(mixer, stereo, frames, paused, 0, NULL, error_out);
}

enum mh_error_code
mh_mixer_add(struct mh_mixer *mixer, float *stereo, size_t frames, int paused,
	struct mh_error *error_out)
{
	return mix_frames(mixer, stereo, frames, paused, 1, NULL, error_out);
}

enum mh_error_code
mh_mixer_render_processed(struct mh_mixer *mixer, float *stereo, size_t frames,
	int paused, const struct mh_mixer_processor *processor,
	struct mh_error *error_out)
{
	return mix_frames(
		mixer, stereo, frames, paused, 0, processor, error_out);
}

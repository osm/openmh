#include <string.h>

#include "engine/audio/effect_pcm.h"
#include "engine/audio/effects_internal.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

struct pcm_stream {
	struct mh_effect_pcm_channel	channels[MH_SOUND_CHANNELS];

	unsigned char
		bytes[MH_EFFECT_PCM_FRAMES_MAX * MH_EFFECT_PCM_FRAME_BYTES];
	size_t				remaining, available, position;
};

static enum mh_error_code
load_pcm_channels(const struct mh_effects_mixer *effects,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	struct mh_error *error_out)
{
	const struct mh_mixer_voice *voice;
	struct mh_effect_pcm_channel *channel;
	unsigned int i;

	memset(channels, 0, sizeof(*channels) * MH_SOUND_CHANNELS);

	for (i = 0; i < MH_SOUND_CHANNELS; i++) {
		channel = &channels[i];
		voice = &effects->pcm.voices[i];

		if (!(voice->left >= 0 && voice->left <= 1 &&
			voice->right >= 0 && voice->right <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid physical effects gains");

		channel->wave = voice->wave;
		channel->cursor = effects->cursors[i];
		channel->fraction = effects->fractions[i];
		channel->whole_step = effects->steps[i].whole;
		channel->fraction_step = effects->steps[i].fraction;
		channel->loop_start = effects->loop_start[i];
		channel->loop_end = effects->loop_end[i];
		channel->left =
			(float)((float)voice->left * MH_EFFECT_PCM_GAIN_MAX);
		channel->right =
			(float)((float)voice->right * MH_EFFECT_PCM_GAIN_MAX);
		channel->flags = (voice->active ? MH_EFFECT_PCM_ACTIVE : 0U) |
			(voice->loop_end > voice->loop_start
					? MH_EFFECT_PCM_LOOP
					: 0U);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
store_pcm_channels(struct mh_effects_mixer *effects,
	const struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS])
{
	const struct mh_effect_pcm_channel *channel;
	unsigned int i;

	for (i = 0; i < MH_SOUND_CHANNELS; i++) {
		channel = &channels[i];
		effects->cursors[i] = channel->cursor;
		effects->fractions[i] = channel->fraction;
		effects->pcm.voices[i].active =
			(channel->flags & MH_EFFECT_PCM_ACTIVE) != 0;
		effects->pcm.voices[i].cursor = channel->cursor +
			channel->fraction / (double)MH_U32_MODULUS;
	}
}

static enum mh_error_code
service_buffer(struct mh_effects_mixer *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const *banks, unsigned int count,
	struct mh_effect_buffer *buffer,
	const struct mh_effect_buffer_driver *driver, void *context,
	unsigned long offset, int *filled, struct mh_error *error_out)
{
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS];
	enum mh_error_code status;

	if (!mh_effects_banks_valid(effects, pool, banks, count) ||
		effects->output_ready || effects->phase != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects buffer banks or prefetched output");

	status = load_pcm_channels(effects, channels, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (filled != NULL)
		status = mh_effect_buffer_fill(buffer, channels, driver,
			context, offset, filled, error_out);
	else
		status = mh_effect_buffer_poll(
			buffer, channels, driver, context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	store_pcm_channels(effects, channels);
	mh_effects_publish_cursors(effects, pool, banks);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_effects_buffer_fill(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_effect_buffer *buffer,
	const struct mh_effect_buffer_driver *driver, void *context,
	unsigned long offset, int *filled, struct mh_error *error_out)
{
	if (filled == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing effects buffer result");

	return service_buffer(effects, pool, banks, count, buffer, driver,
		context, offset, filled, error_out);
}

enum mh_error_code
mh_effects_buffer_poll(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_effect_buffer *buffer,
	const struct mh_effect_buffer_driver *driver, void *context,
	struct mh_error *error_out)
{
	return service_buffer(effects, pool, banks, count, buffer, driver,
		context, 0, NULL, error_out);
}

static enum mh_error_code
read_pcm_frame(struct pcm_stream *source, unsigned long *word,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (source->position == source->available) {
		if (source->remaining == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"effects resampler exhausted PCM");

		source->available = source->remaining > MH_EFFECT_PCM_FRAMES_MAX
			? MH_EFFECT_PCM_FRAMES_MAX
			: source->remaining;
		status = mh_effect_pcm_render(source->channels, source->bytes,
			source->available, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		source->remaining -= source->available;
		source->position = 0;
	}

	*word = mh_read_u32le(
		source->bytes + source->position * MH_EFFECT_PCM_FRAME_BYTES);
	source->position++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
unpack_sample(unsigned long packed, unsigned int side)
{
	unsigned long value;

	value = (packed >> (side * 16)) & (unsigned long)MH_U16_MAX;

	return value >= MH_U16_SIGN_BIT ? (double)value - MH_U16_MODULUS
					: (double)value;
}

enum mh_error_code
mh_effects_render(struct mh_effects_mixer *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const *banks, unsigned int count, float *stereo,
	size_t frames, int paused, struct mh_error *error_out)
{
	struct mh_effects_mixer next;
	struct pcm_stream source;
	float output[MH_MIXER_FRAMES_MAX * 2];
	unsigned int side;
	size_t frame;
	unsigned long packed = 0;
	double value, a, b, scale;
	enum mh_error_code status;

	if (!mh_effects_banks_valid(effects, pool, banks, count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid effects banks");

	if (stereo == NULL || frames > MH_MIXER_FRAMES_MAX ||
		effects->pcm.rate < MH_MIXER_RATE_MIN ||
		effects->pcm.rate > MH_MIXER_RATE_MAX ||
		!(effects->pcm.gain >= 0 && effects->pcm.gain <= 1) ||
		effects->phase >= effects->pcm.rate + MH_EFFECT_PCM_RATE ||
		(effects->output_ready != 0 && effects->output_ready != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects output request");

	if (paused) {
		memset(stereo, 0, frames * 2 * sizeof(*stereo));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	next = *effects;
	memset(&source, 0, sizeof(source));
	status = load_pcm_channels(&next, source.channels, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_effect_pcm_render(
			source.channels, source.bytes, 0, error_out);

	if (status != MH_ERROR_NONE || frames == 0)
		return status;

	/*
	 * Keep the existing caller gain convention while moving the mandatory
	 * 1/8 contribution scale and signed-16 rounding inside the device core.
	 */
	scale = next.pcm.gain * 8 / MH_U16_SIGN_BIT;

	if (next.pcm.rate == MH_EFFECT_PCM_RATE)
		source.remaining = frames;
	else {
		source.remaining =
			(size_t)((next.phase +
					(unsigned long)(frames - 1) *
						MH_EFFECT_PCM_RATE) /
				next.pcm.rate);

		if (!next.output_ready) {
			source.remaining += 2;
			status = read_pcm_frame(
				&source, &next.held[0], error_out);

			if (status == MH_ERROR_NONE)
				status = read_pcm_frame(
					&source, &next.held[1], error_out);

			if (status != MH_ERROR_NONE)
				return status;

			next.output_ready = 1;
		}
	}

	for (frame = 0; frame < frames; frame++) {
		if (next.pcm.rate == MH_EFFECT_PCM_RATE) {
			status = read_pcm_frame(&source, &packed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			next.held[0] = next.held[1] = packed;
		} else
			while (next.phase >= next.pcm.rate) {
				next.held[0] = next.held[1];
				status = read_pcm_frame(
					&source, &next.held[1], error_out);

				if (status != MH_ERROR_NONE)
					return status;

				next.phase -= next.pcm.rate;
			}

		for (side = 0; side < 2; side++) {
			a = unpack_sample(next.held[0], side);
			b = unpack_sample(next.held[1], side);
			value = (a + (b - a) * next.phase / next.pcm.rate) *
				scale;
			output[frame * 2 + side] = (float)(value < -1 ? -1
					: value > 1		      ? 1
								: value);
		}

		if (next.pcm.rate != MH_EFFECT_PCM_RATE)
			next.phase += MH_EFFECT_PCM_RATE;
	}

	store_pcm_channels(&next, source.channels);
	*effects = next;
	memcpy(stereo, output, frames * 2 * sizeof(*stereo));
	mh_effects_publish_cursors(effects, pool, banks);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

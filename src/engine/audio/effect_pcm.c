#include <string.h>

#include "engine/audio/effect_pcm.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define PCM_SOURCE_FRAMES_MAX 32000000UL

static unsigned long
scale_sample_word(long sample, float gain)
{
	double value, fraction;
	unsigned long whole;

	/*
	 * A binary32 gain times a signed-16 integer fits exactly in binary64.
	 * Implement nearest/even conversion without changing host FPU
	 * state or depending on C99 rounding interfaces.
	 */
	value = sample * (double)gain;

	if (value < 0)
		value = -value;

	whole = (unsigned long)value;
	fraction = value - whole;

	if (fraction > .5 || (fraction == .5 && (whole & 1UL)))
		whole++;

	return sample < 0 ? (MH_U16_MODULUS - whole) & (unsigned long)MH_U16_MAX
			: whole;
}

static enum mh_error_code
validate_channels(
	const struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	struct mh_error *error_out)
{
	const struct mh_effect_pcm_channel *channel;
	const struct mh_audio_pcm *wave;
	size_t span;
	unsigned int index;

	for (index = 0; index < MH_SOUND_CHANNELS; index++) {
		channel = &channels[index];

		if (!(channel->flags & MH_EFFECT_PCM_ACTIVE))
			continue;

		wave = channel->wave;

		if (wave == NULL || wave->pcm == NULL || wave->bits != 16 ||
			wave->channels != 1 || wave->frames == 0 ||
			wave->frames > PCM_SOURCE_FRAMES_MAX ||
			wave->frames > wave->bytes / 2 ||
			channel->cursor >= wave->frames ||
			channel->loop_start >= channel->loop_end ||
			channel->loop_end > wave->frames ||
			channel->fraction > MH_U32_MAX ||
			channel->fraction_step > MH_U32_MAX ||
			!(channel->left >= 0 &&
				channel->left <= MH_EFFECT_PCM_GAIN_MAX) ||
			!(channel->right >= 0 &&
				channel->right <= MH_EFFECT_PCM_GAIN_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid effects PCM channel");

		span = channel->loop_end - channel->loop_start;

		if (channel->whole_step >= span &&
			!(span == 1 && channel->whole_step == 1 &&
				channel->fraction_step == 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid effects PCM step");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
mix_channel(struct mh_effect_pcm_channel *channel, unsigned char *stereo,
	size_t frames)
{
	const struct mh_audio_pcm *wave;
	size_t frame, cursor;
	unsigned int carry;
	unsigned long fraction, value, packed;
	long sample;

	wave = channel->wave;
	cursor = channel->cursor;
	fraction = channel->fraction;

	for (frame = 0; frame < frames; frame++) {
		value = mh_read_u16le(wave->pcm + cursor * 2);
		sample = value >= MH_U16_SIGN_BIT
			? (long)value - (long)MH_U16_MODULUS
			: (long)value;
		packed = scale_sample_word(sample, channel->left) |
			(scale_sample_word(sample, channel->right) << 16);

		/*
		 * Packed addition carries the low (left) word into the high
		 * (right) word. Do not sum channels separately.
		 */
		packed = (packed +
				mh_read_u32le(stereo +
					frame * MH_EFFECT_PCM_FRAME_BYTES)) &
			MH_U32_MAX;

		mh_write_u32le(
			stereo + frame * MH_EFFECT_PCM_FRAME_BYTES, packed);

		carry = fraction > MH_U32_MAX - channel->fraction_step;
		fraction = (fraction + channel->fraction_step) & MH_U32_MAX;
		cursor += channel->whole_step + carry;

		if (channel->flags & MH_EFFECT_PCM_LOOP) {
			if (cursor >= channel->loop_end)
				cursor -=
					channel->loop_end - channel->loop_start;
		} else if (cursor >= wave->frames) {
			channel->flags &= ~MH_EFFECT_PCM_ACTIVE;
			break;
		}
	}

	/*
	 * Both original completion branches bypass cursor publication.
	 */
	if (channel->flags & MH_EFFECT_PCM_ACTIVE) {
		channel->cursor = cursor;
		channel->fraction = fraction;
	}
}

enum mh_error_code
mh_effect_pcm_render(struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	unsigned char *stereo, size_t frames, struct mh_error *error_out)
{
	unsigned int index;
	enum mh_error_code status;

	if (channels == NULL || stereo == NULL ||
		frames > MH_EFFECT_PCM_FRAMES_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects PCM request");

	status = validate_channels(channels, error_out);

	if (status != MH_ERROR_NONE || frames == 0)
		return status;

	/*
	 * Zeroing first is equivalent to the first active channel's overwrite
	 * and remainder clear, including the all-inactive branch.
	 */
	memset(stereo, 0, frames * MH_EFFECT_PCM_FRAME_BYTES);

	for (index = 0; index < MH_SOUND_CHANNELS; index++)
		if (channels[index].flags & MH_EFFECT_PCM_ACTIVE)
			mix_channel(&channels[index], stereo, frames);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

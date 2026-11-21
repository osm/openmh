#include <string.h>

#include "engine/audio/effect_output.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define OUTPUT_BYTES_MAX (1024UL * 1024UL)
#define OUTPUT_RATE_MIN 4000
#define OUTPUT_RATE_MAX 192000
#define OUTPUT_FRAMES_MAX 8192

static int
output_valid(const struct mh_effect_output *output)
{
	return output != NULL && output->data != NULL &&
		output->bytes >= MH_EFFECT_BUFFER_FILL_BYTES &&
		output->bytes <= OUTPUT_BYTES_MAX &&
		output->bytes % MH_EFFECT_PCM_FRAME_BYTES == 0 &&
		output->lead_bytes < output->bytes &&
		output->lead_bytes % MH_EFFECT_PCM_FRAME_BYTES == 0 &&
		output->play_offset < output->bytes &&
		output->play_offset % MH_EFFECT_PCM_FRAME_BYTES == 0 &&
		output->rate >= OUTPUT_RATE_MIN &&
		output->rate <= OUTPUT_RATE_MAX && output->phase < output->rate;
}

enum mh_error_code
mh_effect_output_init(unsigned char *storage, size_t bytes,
	unsigned long lead_bytes, unsigned long rate,
	struct mh_effect_output *output, struct mh_error *error_out)
{
	struct mh_effect_output next;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing effects ring output");

	memset(&next, 0, sizeof(next));
	next.data = storage;
	next.bytes = bytes;
	next.lead_bytes = lead_bytes;
	next.rate = rate;

	if (!output_valid(&next))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects ring output");

	next.buffer.write = lead_bytes;
	memset(storage, 0, bytes);
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
ring_status(void *context, unsigned long *value)
{
	struct mh_effect_output *output;

	output = (struct mh_effect_output *)context;

	if (value == NULL)
		return 1;

	*value = 0;

	if (!output_valid(output))
		return 1;

	/* This in-memory device cannot be lost. */
	*value = MH_EFFECT_BUFFER_STATUS_PLAYING |
		MH_EFFECT_BUFFER_STATUS_LOOPING;

	return 0;
}

static unsigned long
ring_position(void *context, unsigned long *play, unsigned long *write)
{
	struct mh_effect_output *output;

	output = (struct mh_effect_output *)context;

	if (!output_valid(output) || play == NULL || write == NULL)
		return 1;

	*play = output->play_offset;
	*write = (output->play_offset + output->lead_bytes) %
		(unsigned long)output->bytes;

	return 0;
}

static unsigned long
ring_restore(void *context)
{
	return output_valid((struct mh_effect_output *)context) ? 0 : 1;
}

static size_t
first_region_bytes(const struct mh_effect_output *output, size_t offset)
{
	size_t bytes;

	bytes = output->bytes - offset;

	return bytes > MH_EFFECT_BUFFER_FILL_BYTES ? MH_EFFECT_BUFFER_FILL_BYTES
						: bytes;
}

static unsigned long
ring_lock(void *context, unsigned long offset, unsigned long bytes,
	struct mh_effect_buffer_region regions[2])
{
	struct mh_effect_output *output;
	size_t first_bytes, start_offset;

	output = (struct mh_effect_output *)context;

	if (!output_valid(output) || output->locked || regions == NULL ||
		bytes != MH_EFFECT_BUFFER_FILL_BYTES ||
		offset % MH_EFFECT_PCM_FRAME_BYTES != 0)
		return 1;

	start_offset = (size_t)(offset % (unsigned long)output->bytes);
	first_bytes = first_region_bytes(output, start_offset);

	regions[0].data = output->data + start_offset;
	regions[0].bytes = first_bytes;
	regions[1].data = first_bytes == MH_EFFECT_BUFFER_FILL_BYTES
		? NULL
		: output->data;
	regions[1].bytes = MH_EFFECT_BUFFER_FILL_BYTES - first_bytes;
	output->locked_offset = start_offset;
	output->locked = 1;

	return 0;
}

static unsigned long
ring_unlock(void *context, const struct mh_effect_buffer_region regions[2])
{
	struct mh_effect_output *output;
	size_t first_bytes;

	output = (struct mh_effect_output *)context;

	if (!output_valid(output) || !output->locked || regions == NULL ||
		output->locked_offset >= output->bytes)
		return 1;

	first_bytes = first_region_bytes(output, output->locked_offset);

	if (regions[0].data != output->data + output->locked_offset ||
		regions[0].bytes != first_bytes ||
		regions[1].bytes != MH_EFFECT_BUFFER_FILL_BYTES - first_bytes ||
		regions[1].data !=
			(first_bytes == MH_EFFECT_BUFFER_FILL_BYTES
					? NULL
					: output->data))
		return 1;

	output->locked = 0;
	output->fills = (output->fills + 1) & MH_U32_MAX;

	if (regions[1].bytes != 0)
		output->wraps = (output->wraps + 1) & MH_U32_MAX;

	return 0;
}

const struct mh_effect_buffer_driver *
mh_effect_output_driver(void)
{
	static const struct mh_effect_buffer_driver driver = {ring_status,
		ring_position, ring_restore, ring_lock, ring_unlock};

	return &driver;
}

static double
read_sample(const unsigned char *bytes)
{
	unsigned long value;

	value = mh_read_u16le(bytes);

	return value >= MH_U16_SIGN_BIT ? (double)value - MH_U16_MODULUS
					: (double)value;
}

enum mh_error_code
mh_effect_output_render(struct mh_effect_output *output, double gain,
	float *stereo, size_t frames, int paused, struct mh_error *error_out)
{
	unsigned long play_offset, phase, advance_frames, consumed_frames;
	size_t frame, next_offset;
	unsigned int side;
	double a, b, value, scale;

	if (!output_valid(output) || output->locked || stereo == NULL ||
		frames > OUTPUT_FRAMES_MAX || !(gain >= 0 && gain <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects ring render");

	if (paused) {
		memset(stereo, 0, frames * 2 * sizeof(*stereo));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	play_offset = output->play_offset;
	phase = output->phase;
	consumed_frames = output->consumed_frames;

	/* Undo the original 1/8 contribution scale before s16 normalization. */
	scale = gain * 8 / MH_U16_SIGN_BIT;

	for (frame = 0; frame < frames; frame++) {
		next_offset =
			(size_t)((play_offset + MH_EFFECT_PCM_FRAME_BYTES) %
				(unsigned long)output->bytes);

		for (side = 0; side < 2; side++) {
			a = read_sample(output->data + play_offset + side * 2);
			b = read_sample(output->data + next_offset + side * 2);
			value = (a + (b - a) * phase / output->rate) * scale;
			stereo[frame * 2 + side] = (float)(value < -1 ? -1
					: value > 1		      ? 1
								: value);
		}

		phase += MH_EFFECT_PCM_RATE;
		advance_frames = phase / output->rate;
		phase %= output->rate;
		play_offset =
			(play_offset +
				advance_frames * MH_EFFECT_PCM_FRAME_BYTES) %
			(unsigned long)output->bytes;
		consumed_frames =
			(consumed_frames + advance_frames) & MH_U32_MAX;
	}

	output->play_offset = play_offset;
	output->phase = phase;
	output->consumed_frames = consumed_frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_effect_output_clear(
	struct mh_effect_output *output, struct mh_error *error_out)
{
	if (!output_valid(output) || output->locked)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects ring clear");

	memset(output->data, 0, output->bytes);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

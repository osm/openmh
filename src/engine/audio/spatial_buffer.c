#include "engine/audio/spatial_buffer.h"
#include "engine/core/limits.h"

#define MIN_FADE_SAMPLES 100UL

static unsigned long
fade_samples(unsigned long count, unsigned long samples)
{
	if (count < MIN_FADE_SAMPLES)
		return 0;

	return count > samples ? samples : count;
}

enum mh_error_code
mh_spatial_buffer_prepare(const struct mh_audio_pcm *wave,
	const struct mh_spatial_buffer_input *input,
	struct mh_spatial_buffer_plan *output, struct mh_error *error_out)
{
	struct mh_spatial_buffer_plan plan;
	unsigned long samples, fade_in, fade_out, swap;
	size_t sample_bytes;

	if (wave == NULL || input == NULL || output == NULL ||
		wave->pcm == NULL || wave->bytes == 0 ||
		(wave->bits != 8 && wave->bits != 16))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial buffer input");

	if (wave->bytes > MH_U32_MAX || input->fade_in_samples > MH_U32_MAX ||
		input->fade_out_samples > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"spatial buffer outside original range");

	sample_bytes = wave->bits / 8;
	samples = (unsigned long)(wave->bytes / sample_bytes);
	fade_in = fade_samples(input->fade_in_samples, samples);
	fade_out = fade_samples(input->fade_out_samples, samples);

	if (samples - fade_out < fade_in) {
		swap = fade_in;
		fade_in = fade_out;
		fade_out = swap;
	}

	if (samples - fade_out == fade_in)
		fade_in = fade_out = 0;

	/* Check before adding or multiplying the original unsigned counts. */
	if (fade_in > samples - fade_out)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"spatial buffer cuts overlap");

	plan.offset = fade_in * sample_bytes;
	plan.bytes = wave->bytes - (fade_in + fade_out) * sample_bytes;
	plan.fade_in_samples = fade_in;
	plan.fade_out_samples = fade_out;
	plan.spatial = input->spatial != 0;
	plan.software_only = input->software_only != 0;
	plan.near_distance = input->near_distance;
	plan.far_distance = input->far_distance;

	if (plan.spatial && plan.near_distance < 1)
		plan.near_distance = 1;

	*output = plan;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

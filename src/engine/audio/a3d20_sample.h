#ifndef MH_AUDIO_A3D20_SAMPLE_H
#define MH_AUDIO_A3D20_SAMPLE_H

#include "engine/audio/spatial_buffer.h"

struct mh_a3d20_sample_input {
	const char	*name;
	unsigned long	fade_in_samples, fade_out_samples;
	float		near_distance, far_distance;
};

struct mh_a3d20_sample_plan {
	struct mh_spatial_buffer_plan	buffer;
	size_t				duplicate_index;
};

/*
 * Names are borrowed, NUL-terminated and ordered newest first. The first
 * exact match selects a duplicate; duplicate_index == count means a new
 * sample. Even duplicates validate and normalize the requested PCM view.
 * For duplicates, buffer offsets/lengths describe that request only: reuse
 * the existing payload without reading or trimming it again. Apply the new
 * fade counts and distance limits to the new voice in either case. Spatial
 * distances use independent minimums of 1 (near) and 1.1 (far).
 * No device access or ownership transfer; output is unchanged on failure.
 */
enum mh_error_code	mh_a3d20_sample_prepare(const struct mh_audio_pcm *wave,
	const struct mh_a3d20_sample_input *input, const char *const names[],
	size_t count, struct mh_a3d20_sample_plan *output,
	struct mh_error *error_out);

#endif

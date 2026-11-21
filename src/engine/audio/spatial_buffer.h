#ifndef MH_AUDIO_SPATIAL_BUFFER_H
#define MH_AUDIO_SPATIAL_BUFFER_H

#include "engine/core/error.h"
#include "interfaces/audio/pcm.h"

struct mh_spatial_buffer_input {
	int		spatial, software_only;
	unsigned long	fade_in_samples, fade_out_samples;
	float		near_distance, far_distance;
};

struct mh_spatial_buffer_plan {
	size_t		offset, bytes;
	unsigned long	fade_in_samples, fade_out_samples;
	int		spatial, software_only;
	float		near_distance, far_distance;
};

/*
 * Plan a static buffer from a parsed PCM view; no allocation or device access.
 * Offsets are relative to wave->pcm. Counts use individual channel samples,
 * not frames: stereo cuts may fall between channels, as in the original.
 * Output is unchanged on failure. Distances must be finite.
 *
 * The caller selects spatial/software placement and profile distance limits.
 * software_only is a placement request, not a report of actual capabilities.
 * Spatial near distance is at least one; nonspatial distances are unchanged.
 * Device ownership, loading defaults and failure cleanup belong to the caller.
 */
enum mh_error_code	mh_spatial_buffer_prepare(
	const struct mh_audio_pcm *wave,
	const struct mh_spatial_buffer_input *input,
	struct mh_spatial_buffer_plan *output, struct mh_error *error_out);

#endif

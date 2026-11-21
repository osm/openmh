#ifndef MH_SPATIAL_BANK_H
#define MH_SPATIAL_BANK_H

#include "engine/audio/mixer.h"
#include "engine/audio/spatial_buffer.h"
#include "engine/audio/spatial_source.h"

/*
 * Copy a valid, loaded bank voice before software ear saturation; no
 * scheduling. Nonspatial sources retain the caller's existing geometry and
 * distances.
 */
void	mh_spatial_bank_source(struct mh_spatial_source_input *input,
	const struct mh_mixer *bank, unsigned int voice);

/*
 * Borrow a frame-aligned trimmed view using the supplied placement. The
 * source PCM must outlive the returned view. A frame-alignment failure leaves
 * wave unchanged, but plan already contains the raw sample cuts.
 */
enum mh_error_code	mh_spatial_bank_sample(const struct mh_mixer *bank,
	unsigned int voice, int spatial, int software_only,
	const struct mh_spatial_source_input *source, struct mh_audio_pcm *wave,
	struct mh_spatial_buffer_plan *plan, struct mh_error *error_out);

#endif

#ifndef MH_AUDIO_SAMPLE_H
#define MH_AUDIO_SAMPLE_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/audio/pcm.h"

struct mh_audio_decoder_provider;
struct mh_resource_source;

#define MH_AUDIO_SAMPLE_BYTES_MAX (128UL * 1024UL * 1024UL)

/*
 * Load decoded PCM through the selected decoder. Limit bounds decoded bytes,
 * independently of encoded-file size. The returned view borrows storage->data;
 * the caller frees that allocation. Both outputs are unchanged on failure.
 * Provider/resources/path are borrowed only during this call; no decoder or
 * resource handle escapes. Read failures retain their status with a stable
 * diagnostic because decoder-owned messages cannot survive close.
 */
enum mh_error_code	mh_audio_sample_load_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *path,
	size_t limit, struct mh_file *storage, struct mh_audio_pcm *output,
	struct mh_error *error_out);

#endif

#ifndef MH_BACKENDS_AUDIO_WAVE_METADATA_H
#define MH_BACKENDS_AUDIO_WAVE_METADATA_H

#include "interfaces/audio/decoder.h"

/* Provider callback; reads bounded metadata ranges without loading PCM. */
enum mh_error_code	mh_audio_wave_inspect(void *context,
	const struct mh_resource_source *resources, const char *path,
	struct mh_audio_decode_info *output, struct mh_error *error_out);

#endif

#ifndef MH_PAUSE_SOUND_ASSETS_H
#define MH_PAUSE_SOUND_ASSETS_H

#include "engine/audio/pause_sound.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;

/* Owned pause samples, at most 64 MiB. Loading errors preserve output. */
struct mh_pause_sound_assets {
	struct mh_pause_sound_bank	bank;
	const char			*names[MH_PAUSE_SOUND_SAMPLES];
	struct mh_file			files[MH_PAUSE_SOUND_SAMPLES];
};

/*
 * Missing pause cues remain silent; present malformed samples are errors.
 * Source is borrowed only while loading; output owns the sample bytes.
 */
enum mh_error_code	mh_pause_sound_assets_load_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_pause_sound_assets *output, struct mh_error *error_out);
void			mh_pause_sound_assets_free(
	struct mh_pause_sound_assets *assets);

#endif

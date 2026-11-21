#ifndef MH_AUDIO_MUSIC_ASSET_H
#define MH_AUDIO_MUSIC_ASSET_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/audio/pcm.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;

struct mh_music_asset {
	struct mh_file		file;
	struct mh_audio_pcm	wave;
};

/*
 * Owns one CD track and its borrowed PCM view. Free before reuse; loading
 * errors leave output unchanged. Music must decode to 44.1 or 48 kHz, stereo,
 * 16-bit PCM. Provider and source are borrowed only while loading. file owns
 * decoded PCM bytes, without an encoded container header.
 */
enum mh_error_code	mh_music_asset_load_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *directory,
	unsigned int track, struct mh_music_asset *output,
	struct mh_error *error_out);
void			mh_music_asset_free(struct mh_music_asset *asset);

#endif

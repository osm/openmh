#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/audio/music_asset.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"
#include "interfaces/audio/decoder.h"

#define CD_FIRST_AUDIO_TRACK 2
#define CD_LAST_TRACK 99
#define CD_SAMPLE_RATE 44100
#define OPUS_SAMPLE_RATE 48000
#define CD_CHANNELS 2
#define CD_SAMPLE_BITS 16

void
mh_music_asset_free(struct mh_music_asset *asset)
{
	free(asset->file.data);
	memset(asset, 0, sizeof(*asset));
}

enum mh_error_code
mh_music_asset_load_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *directory,
	unsigned int track, struct mh_music_asset *output,
	struct mh_error *error_out)
{
	struct mh_music_asset asset;
	const char *extension;
	char name[32], path[1024];
	enum mh_error_code status;

	if (directory == NULL || output == NULL ||
		track < CD_FIRST_AUDIO_TRACK || track > CD_LAST_TRACK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD track request");

	memset(&asset, 0, sizeof(asset));
	extension = mh_audio_decoder_extension(decoder);

	if (extension == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"music decoding is unavailable");

	sprintf(name, "track-%02u.%s", track, extension);
	status = mh_audio_sample_path_from(decoder, source, directory, name,
		path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_audio_sample_load_from(decoder, source, path,
			MH_AUDIO_SAMPLE_BYTES_MAX, &asset.file, &asset.wave,
			error_out);

	if (status == MH_ERROR_NONE &&
		((asset.wave.rate != CD_SAMPLE_RATE &&
			asset.wave.rate != OPUS_SAMPLE_RATE) ||
			asset.wave.channels != CD_CHANNELS ||
			asset.wave.bits != CD_SAMPLE_BITS))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"expected music PCM: 44100 or 48000 Hz, stereo, 16 bits");

	if (status != MH_ERROR_NONE) {
		mh_music_asset_free(&asset);
		return status;
	}

	*output = asset;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

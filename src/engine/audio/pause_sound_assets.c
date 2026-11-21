#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/pause_sound_assets.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"

#define SAMPLE_BYTES_LIMIT (64UL * 1024 * 1024)

void
mh_pause_sound_assets_free(struct mh_pause_sound_assets *assets)
{
	unsigned int i;

	if (assets == NULL)
		return;

	for (i = 0; i < MH_PAUSE_SOUND_SAMPLES; i++)
		free(assets->files[i].data);

	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_pause_sound_assets_load_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_pause_sound_assets *output, struct mh_error *error_out)
{
	static const char *const names[MH_PAUSE_SOUND_SAMPLES] = {
		MH_ASSET_PAUSE_LOOP, MH_ASSET_PAUSE_ENTER,
		MH_ASSET_PAUSE_CURSOR, MH_ASSET_PAUSE_ERROR};

	struct mh_pause_sound_assets assets;
	enum mh_error_code status;
	char path[1024];
	unsigned int i;
	size_t total;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sound assets");

	memset(&assets, 0, sizeof(assets));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	total = 0;

	for (i = 0; i < MH_PAUSE_SOUND_SAMPLES && status == MH_ERROR_NONE;
		i++) {
		assets.names[i] = names[i];
		status = mh_audio_sample_path_from(decoder, source, root,
			names[i], path, sizeof(path), error_out);

		if (status == MH_ERROR_NOT_FOUND) {
			assets.bank.absent_samples |= 1U << i;
			status = MH_ERROR_NONE;
			continue;
		}

		if (status == MH_ERROR_NONE)
			status = mh_audio_sample_load_from(decoder, source,
				path, SAMPLE_BYTES_LIMIT - total,
				&assets.files[i], &assets.bank.waves[i],
				error_out);

		if (status == MH_ERROR_NONE)
			total += assets.files[i].size;
	}

	if (status != MH_ERROR_NONE) {
		mh_pause_sound_assets_free(&assets);
		return status;
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

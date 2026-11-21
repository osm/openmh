#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/ambient_assets.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"
#include "engine/storage/asset_path.h"
#include "interfaces/animation/source.h"
#include "interfaces/audio/environment_source.h"
#include "interfaces/storage/resource.h"

#define SAMPLE_BYTES_LIMIT (64UL * 1024 * 1024)

void
mh_ambient_assets_free(struct mh_ambient_assets *assets)
{
	unsigned int i;

	for (i = 0; i < MH_MIXER_VOICES; i++)
		free(assets->files[i].data);

	mh_scene_free(&assets->scene);
	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_ambient_assets_load_from(const struct mh_env_sound_source *env_sound_source,
	const struct mh_animation_source *animation_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *esd, const char *scene, struct mh_ambient_assets *output,
	struct mh_error *error_out)
{
	struct mh_env_sounds definitions;
	struct mh_ambient_assets assets;
	char relative[128], path[1024];
	enum mh_error_code status;
	unsigned int j, entry;
	size_t total, k;

	if (root == NULL || esd == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ambience request");

	memset(&assets, 0, sizeof(assets));
	status = mh_env_sounds_load_from(
		env_sound_source, source, root, esd, &definitions, error_out);
	total = 0;

	if (status != MH_ERROR_NONE)
		return status;

	if (scene != NULL && scene[0] != 0)
		status = mh_scene_load_from(animation_source, source, root,
			scene, &assets.scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ambient_bind(
			&definitions, &assets.scene, &assets.bank, error_out);

	for (entry = 0; status == MH_ERROR_NONE && entry < assets.bank.count;
		entry++) {
		for (k = 0; assets.bank.sounds[entry].sample[k] != 0; k++)
			if (assets.bank.sounds[entry].sample[k] >= 'A' &&
				assets.bank.sounds[entry].sample[k] <= 'Z')
				assets.bank.sounds[entry].sample[k] +=
					'a' - 'A';

		for (j = 0; j < entry; j++)
			if (strcmp(assets.bank.sounds[j].sample,
				assets.bank.sounds[entry].sample) == 0)
				break;

		if (j < entry)
			assets.bank.waves[entry] = assets.bank.waves[j];
		else {
			sprintf(relative, MH_ASSET_SOUND_FORMAT,
				assets.bank.sounds[entry].sample);
			status = mh_audio_sample_path_from(decoder, source,
				root, relative, path, sizeof(path), error_out);

			if (status == MH_ERROR_NONE)
				status = mh_audio_sample_load_from(decoder,
					source, path,
					SAMPLE_BYTES_LIMIT - total,
					&assets.files[entry],
					&assets.bank.waves[entry], error_out);

			if (status == MH_ERROR_NONE)
				total += assets.files[entry].size;
		}
	}

	if (status != MH_ERROR_NONE) {
		mh_ambient_assets_free(&assets);
		return status;
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

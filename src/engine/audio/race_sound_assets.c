#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/horn.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"
#include "interfaces/audio/horn_source.h"

#define HORN_BYTES_LIMIT (64UL * 1024 * 1024)

void
mh_race_sound_assets_free(struct mh_race_sound_assets *assets)
{
	unsigned int i;

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		free(assets->files[i].data);

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		free(assets->horn_files[i].data);

	memset(assets, 0, sizeof(*assets));
}

static int
optional_sample(unsigned int sample)
{
	return sample == MH_SOUND_TURBO || sample == MH_SOUND_FINISH ||
		sample == MH_SOUND_ELIMINATED || sample == MH_SOUND_MESSAGE;
}

static enum mh_error_code
load_samples(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_race_sound_assets *assets, struct mh_error *error_out)
{
	static const char *const names[MH_RACE_SAMPLES] = {
		MH_ASSET_SAMPLE_GASON, MH_ASSET_SAMPLE_GASRELE,
		MH_ASSET_SAMPLE_GASOTL, MH_ASSET_SAMPLE_GASOTR,
		MH_ASSET_SAMPLE_GASRTL, MH_ASSET_SAMPLE_GASRTR,
		MH_ASSET_SAMPLE_INNEEN, MH_ASSET_SAMPLE_TURBO,
		MH_ASSET_SAMPLE_ENEMYENG, MH_ASSET_SAMPLE_GEAR3,
		MH_ASSET_SAMPLE_COUNTDOWN, MH_ASSET_SAMPLE_GO,
		MH_ASSET_SAMPLE_CHECKPOINT, MH_ASSET_SAMPLE_FINISH2,
		MH_ASSET_SAMPLE_ELIMINATED1, MH_ASSET_SAMPLE_MESSAGE};

	char relative[144], path[1024];
	enum mh_error_code status;
	unsigned int i;

	for (i = 0; i < MH_RACE_SAMPLES; i++) {
		assets->names[i] = names[i];
		sprintf(relative, MH_ASSET_SOUND_FORMAT, names[i]);
		status = mh_audio_sample_path_from(decoder, source, root,
			relative, path, sizeof(path), error_out);

		if (status == MH_ERROR_NOT_FOUND && optional_sample(i)) {
			assets->bank.absent_samples |= 1U << i;
			continue;
		}

		if (status == MH_ERROR_NONE)
			status = mh_audio_sample_load_from(decoder, source,
				path, MH_AUDIO_SAMPLE_BYTES_MAX,
				&assets->files[i], &assets->bank.waves[i],
				error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_horns(const struct mh_horn_source *horn_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *const *horn_names, struct mh_race_sound_assets *assets,
	struct mh_error *error_out)
{
	struct mh_horn_catalog catalog;
	char relative[144], path[1024];
	enum mh_error_code status;
	const char *name;
	unsigned int i, j, default_horn, selected[MH_RACE_MAX_CARS];
	size_t total;

	total = 0;
	status = mh_horn_catalog_load_from(
		horn_source, source, root, &catalog, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_horn_default(&catalog, &default_horn, error_out);

	for (i = 0; i < MH_RACE_MAX_CARS && status == MH_ERROR_NONE; i++) {
		name = horn_names == NULL || horn_names[i] == NULL
			? catalog.horns[default_horn].name
			: horn_names[i];
		status = mh_horn_find(&catalog, name, &selected[i], error_out);

		if (status != MH_ERROR_NONE)
			break;

		strcpy(assets->horn_samples[i],
			catalog.horns[selected[i]].sample);
		status = mh_horn_parameters(&catalog.horns[selected[i]],
			&assets->bank.horns[i].volume,
			&assets->bank.horns[i].pitch, error_out);

		for (j = 0; j < i; j++)
			if (selected[j] == selected[i])
				break;

		if (j < i)
			assets->bank.horns[i].wave = assets->bank.horns[j].wave;
		else if (status == MH_ERROR_NONE &&
			strcmp(catalog.horns[selected[i]].sample, "<None>") !=
				0) {
			sprintf(relative, MH_ASSET_SOUND_FORMAT,
				catalog.horns[selected[i]].sample);
			status = mh_audio_sample_path_from(decoder, source,
				root, relative, path, sizeof(path), error_out);

			if (status == MH_ERROR_NONE)
				status = mh_audio_sample_load_from(decoder,
					source, path, HORN_BYTES_LIMIT - total,
					&assets->horn_files[i],
					&assets->bank.horns[i].wave, error_out);

			if (status == MH_ERROR_NONE)
				total += assets->horn_files[i].size;
		}
	}

	return status;
}

enum mh_error_code
mh_race_sound_assets_load_from(const struct mh_horn_source *horn_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *const *horn_names, struct mh_race_sound_assets *output,
	struct mh_error *error_out)
{
	struct mh_race_sound_assets assets;
	enum mh_error_code status;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing audio asset output");

	memset(&assets, 0, sizeof(assets));
	status = load_samples(decoder, source, root, &assets, error_out);

	if (status == MH_ERROR_NONE)
		status = load_horns(horn_source, decoder, source, root,
			horn_names, &assets, error_out);

	if (status != MH_ERROR_NONE) {
		mh_race_sound_assets_free(&assets);
		return status;
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

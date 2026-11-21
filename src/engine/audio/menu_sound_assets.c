#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/horn.h"
#include "engine/audio/menu_sound_assets.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

#define SAMPLE_BYTES_LIMIT (64UL * 1024 * 1024)

static enum mh_error_code
load_sample(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *relative, struct mh_file *file, struct mh_audio_pcm *wave,
	size_t *total, struct mh_error *error_out)
{
	enum mh_error_code status;
	char path[1024];

	status = mh_audio_sample_path_from(
		decoder, source, root, relative, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_audio_sample_load_from(decoder, source, path,
			SAMPLE_BYTES_LIMIT - *total, file, wave, error_out);

	if (status == MH_ERROR_NONE)
		*total += file->size;

	return status;
}

void
mh_menu_sound_assets_free(struct mh_menu_sound_assets *assets)
{
	unsigned int i;

	if (assets == NULL)
		return;

	for (i = 0; i < MH_MENU_SOUND_SAMPLES; i++)
		free(assets->files[i].data);

	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_menu_sound_assets_load_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_menu_sound_assets *output, struct mh_error *error_out)
{

	struct mh_menu_sound_assets assets;
	struct mh_file config;
	enum mh_error_code status;
	char path[1024], relative[80];
	unsigned int i, j;
	size_t total;
	float frequency, gain;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend audio assets");

	memset(&assets, 0, sizeof(assets));
	memset(&config, 0, sizeof(config));
	total = 0;
	status = mh_asset_path_from(source, root, MH_ASSET_MENU_SOUND_CONFIG,
		path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read(
			source, path, &config, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_sound_read(config.data, config.size,
			&assets.bank.config, error_out);

	free(config.data);

	for (i = 0; status == MH_ERROR_NONE && i < assets.bank.config.count;
		i++) {
		for (j = 0; j < i; j++)
			if (mh_ascii_equal(assets.bank.config.samples[i].name,
				assets.bank.config.samples[j].name))
				break;

		if (j < i)
			assets.bank.waves[i] = assets.bank.waves[j];
		else {
			sprintf(relative, MH_ASSET_MENU_SOUND_FORMAT,
				assets.bank.config.samples[i].name);
			status = load_sample(decoder, source, root, relative,
				&assets.files[i], &assets.bank.waves[i], &total,
				error_out);
		}

		if (status == MH_ERROR_NONE)
			status = mh_menu_sound_parameters(
				&assets.bank.config.samples[i],
				assets.bank.waves[i].rate, &frequency, &gain,
				error_out);
	}

	if (status != MH_ERROR_NONE)
		mh_menu_sound_assets_free(&assets);
	else
		*output = assets;

	return status;
}

void
mh_menu_horn_assets_free(struct mh_menu_horn_assets *assets)
{
	unsigned int i;

	if (assets == NULL)
		return;

	for (i = 0; i < MH_HORNS; i++)
		free(assets->files[i].data);

	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_menu_horn_assets_load_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_horn_catalog *catalog,
	struct mh_menu_horn_assets *output, struct mh_error *error_out)
{

	struct mh_menu_horn_assets assets;
	struct mh_menu_sound_sample sample;
	enum mh_error_code status;
	char relative[160];
	unsigned int i, j;
	size_t total;
	double volume, pitch;
	float frequency, gain;

	if (root == NULL || catalog == NULL || output == NULL ||
		catalog->count == 0 || catalog->count > MH_HORNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend horn assets");

	memset(&assets, 0, sizeof(assets));
	assets.bank.catalog = *catalog;
	total = 0;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < catalog->count && status == MH_ERROR_NONE; i++) {
		if (memchr(catalog->horns[i].sample, 0,
			sizeof(catalog->horns[i].sample)) == NULL) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated horn sample");
			break;
		}

		status = mh_horn_preview_parameters(
			&catalog->horns[i], &volume, &pitch, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (mh_ascii_equal(catalog->horns[i].sample, "<None>"))
			continue;

		for (j = 0; j < i; j++)
			if (mh_ascii_equal(catalog->horns[i].sample,
				catalog->horns[j].sample))
				break;

		if (j < i)
			assets.bank.waves[i] = assets.bank.waves[j];
		else {
			sprintf(relative, MH_ASSET_SOUND_FORMAT,
				catalog->horns[i].sample);
			status = load_sample(decoder, source, root, relative,
				&assets.files[i], &assets.bank.waves[i], &total,
				error_out);
		}

		sample.volume = (float)volume;
		sample.pitch = (float)pitch;

		if (status == MH_ERROR_NONE)
			status = mh_menu_sound_parameters(&sample,
				assets.bank.waves[i].rate, &frequency, &gain,
				error_out);
	}

	if (status != MH_ERROR_NONE)
		mh_menu_horn_assets_free(&assets);
	else
		*output = assets;

	return status;
}

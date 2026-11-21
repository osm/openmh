#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/audio/sample.h"
#include "engine/audio/sample_path.h"
#include "engine/audio/surface_assets.h"
#include "engine/core/error.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/environment_source.h"
#include "interfaces/material/source.h"
#include "interfaces/storage/resource.h"

#define SAMPLE_BYTES_LIMIT (64UL * 1024 * 1024)
#define CAR_CRASH_PRIORITY 2
#define SAMPLE_CHANNELS 1
#define SAMPLE_BITS 16

void
mh_surface_assets_free(struct mh_surface_assets *assets)
{
	unsigned int i;

	if (assets == NULL)
		return;

	for (i = 0; i < MH_SURFACE_SAMPLES; i++)
		free(assets->files[i].data);

	memset(assets, 0, sizeof(*assets));
}

/* MAT names and ESD classes bound both name length and unique sample count. */
static unsigned int
register_sample(char names[MH_SURFACE_SAMPLES][MH_SURFACE_SAMPLE_NAME_BYTES],
	unsigned int *count, const char *name)
{
	char lower[MH_SURFACE_SAMPLE_NAME_BYTES];
	unsigned int i, j;

	for (i = 0; name[i] != 0; i++)
		lower[i] = name[i] >= 'A' && name[i] <= 'Z'
			? name[i] + 'a' - 'A'
			: name[i];

	lower[i] = 0;

	for (j = 0; j < *count; j++)
		if (strcmp(names[j], lower) == 0)
			return j;

	strcpy(names[j], lower);
	(*count)++;

	return j;
}

static unsigned int
collect_samples(struct mh_surface_bank *bank,
	const struct mh_env_sounds *definitions,
	char names[MH_SURFACE_SAMPLES][MH_SURFACE_SAMPLE_NAME_BYTES],
	int collision_definition[MH_CRASH_CLASSES],
	unsigned int collision_sample[MH_CRASH_CLASSES])
{
	static const char *const fixed[MH_SURFACE_FIXED_SAMPLES] = {
		MH_ASSET_SAMPLE_SLIDE1, MH_ASSET_SAMPLE_SLIDEE1,
		MH_ASSET_SAMPLE_SLIDEE2, MH_ASSET_SAMPLE_SPIN,
		MH_ASSET_SAMPLE_BARREL, MH_ASSET_SAMPLE_CRASH2,
		MH_ASSET_SAMPLE_MAJCRASH, MH_ASSET_SAMPLE_EBURST1,
		MH_ASSET_SAMPLE_EBURST2};

	const struct mh_env_sound *definition;
	struct mh_crash_sample *crash;
	const char *name;
	unsigned int i, j, required;

	for (i = 0; i < MH_SURFACE_FIXED_SAMPLES; i++)
		strcpy(names[i], fixed[i]);

	bank->count = MH_SURFACE_FIXED_SAMPLES;

	for (i = 0; i < bank->materials.count; i++)
		for (j = 0; j < MH_MATERIAL_SOUNDS; j++) {
			name = bank->materials.entries[i].sounds[j].sample;

			if (name[0] == 0)
				continue;

			bank->sample[i][j] =
				register_sample(names, &bank->count, name);
		}

	required = bank->count;

	for (i = 0; i < MH_CRASH_CLASSES; i++)
		collision_definition[i] = -1;

	for (i = 0; i < definitions->count; i++)
		if (definitions->sounds[i].kind == MH_ENV_COLLISION)
			collision_definition[definitions->sounds[i]
					.object_class] = (int)i;

	for (i = 0; i < MH_CRASH_CLASSES; i++)
		if (collision_definition[i] >= 0) {
			definition =
				&definitions->sounds[collision_definition[i]];
			collision_sample[i] = register_sample(
				names, &bank->count, definition->sample);
			crash = &bank->crashes
					.samples[MH_CRASH_CAR_SAMPLES + i];
			crash->enabled = 1;
			crash->priority = definition->priority;
			crash->volume = definition->volume;
			crash->pitch = definition->pitch;
		}

	return required;
}

static enum mh_error_code
load_samples(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_surface_assets *assets, unsigned int required,
	char names[MH_SURFACE_SAMPLES][MH_SURFACE_SAMPLE_NAME_BYTES],
	struct mh_error *error_out)
{
	char relative[80], path[1024];
	enum mh_error_code status;
	unsigned int i;
	size_t total;

	total = 0;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < assets->bank.count && status == MH_ERROR_NONE; i++) {
		sprintf(relative, MH_ASSET_BOUNDED_SOUND_FORMAT,
			MH_SURFACE_SAMPLE_NAME_BYTES - 1, names[i]);
		status = mh_audio_sample_path_from(decoder, source, root,
			relative, path, sizeof(path), error_out);

		if (status == MH_ERROR_NOT_FOUND && i >= required) {
			status = MH_ERROR_NONE;
			continue;
		}

		if (status == MH_ERROR_NONE)
			status = mh_audio_sample_load_from(decoder, source,
				path, SAMPLE_BYTES_LIMIT - total,
				&assets->files[i], &assets->bank.waves[i],
				error_out);

		if (status == MH_ERROR_NONE &&
			(assets->bank.waves[i].channels != SAMPLE_CHANNELS ||
				assets->bank.waves[i].bits != SAMPLE_BITS))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"original surface sounds require mono 16-bit PCM");

		if (status == MH_ERROR_NONE)
			total += assets->files[i].size;
	}

	return status == MH_ERROR_NONE
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: status;
}

static void
bind_crashes(struct mh_surface_bank *bank,
	const int collision_definition[MH_CRASH_CLASSES],
	const unsigned int collision_sample[MH_CRASH_CLASSES])
{
	static const unsigned int car_samples[MH_CRASH_CAR_SAMPLES] = {
		MH_SURFACE_SAMPLE_BARREL, MH_SURFACE_SAMPLE_CRASH,
		MH_SURFACE_SAMPLE_MAJOR_CRASH, MH_SURFACE_SAMPLE_MAJOR_CRASH};

	struct mh_crash_sample *crash;
	unsigned int i;

	for (i = 0; i < MH_CRASH_CAR_SAMPLES; i++) {
		crash = &bank->crashes.samples[i];
		crash->enabled = 1;
		crash->priority = CAR_CRASH_PRIORITY;
		crash->volume = (double)(float).7;
		crash->pitch =
			i < MH_CRASH_LIGHT_SAMPLES ? (double)(float).8 : 1;
		crash->wave = bank->waves[car_samples[i]];
	}

	for (i = 0; i < MH_CRASH_CLASSES; i++)
		if (collision_definition[i] >= 0) {
			crash = &bank->crashes
					.samples[MH_CRASH_CAR_SAMPLES + i];
			crash->wave = bank->waves[collision_sample[i]];
			crash->absent = crash->wave.pcm == NULL;
		}
}

enum mh_error_code
mh_surface_assets_load_from(const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *srf, const char *esd, struct mh_surface_assets *output,
	struct mh_error *error_out)
{
	struct mh_surface_assets *assets;
	struct mh_env_sounds definitions;
	char names[MH_SURFACE_SAMPLES][MH_SURFACE_SAMPLE_NAME_BYTES];
	enum mh_error_code status;
	unsigned int collision_sample[MH_CRASH_CLASSES], required;
	int collision_definition[MH_CRASH_CLASSES];

	if (root == NULL || srf == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface asset request");

	assets = (struct mh_surface_assets *)calloc(1, sizeof(*assets));

	if (assets == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"surface asset allocation failed");

	memset(&definitions, 0, sizeof(definitions));
	status = mh_materials_load_from(material_source, source, root, srf,
		&assets->bank.materials, error_out);

	if (status != MH_ERROR_NONE) {
		free(assets);
		return status;
	}

	if (esd != NULL && esd[0] != 0) {
		status = mh_env_sounds_load_from(env_sound_source, source, root,
			esd, &definitions, error_out);

		if (status != MH_ERROR_NONE) {
			free(assets);
			return status;
		}
	}

	required = collect_samples(&assets->bank, &definitions, names,
		collision_definition, collision_sample);
	status = load_samples(
		decoder, source, root, assets, required, names, error_out);

	if (status == MH_ERROR_NONE) {
		bind_crashes(
			&assets->bank, collision_definition, collision_sample);
		memcpy(assets->names, names,
			assets->bank.count * sizeof(names[0]));
		*output = *assets;
	} else
		mh_surface_assets_free(assets);

	free(assets);

	return status;
}

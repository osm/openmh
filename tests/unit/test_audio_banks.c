#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/material/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/tbl.h"
#include "engine/audio/ambient_assets.h"
#include "engine/audio/menu_sound_assets.h"
#include "engine/audio/pause_sound_assets.h"
#include "engine/audio/pcm.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/sample_path.h"
#include "engine/audio/surface_assets.h"
#include "interfaces/audio/decoder_backend.h"
#include "interfaces/audio/horn_source.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "audio banks:%d: %s\n", __LINE__, #e); \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	opens, closes;
	int		fail;
};

struct raw_sample {
	struct mh_file	file;
	size_t		position;
};

static enum mh_error_code
inspect(void *context, const struct mh_resource_source *source,
	const char *path, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	unsigned long size;
	enum mh_error_code status;

	(void)context;
	status = mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size != 8)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid test sample");

	info->frames = 4;
	info->rate = 22050;
	info->channels = 1;
	info->bits = 16;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_sample(void *context, const struct mh_resource_source *source,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct raw_sample *sample;
	enum mh_error_code status;

	fixture = context;
	status = inspect(context, source, path, info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sample = calloc(1, sizeof(*sample));

	if (sample == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation");

	status =
		mh_resource_source_read(source, path, &sample->file, error_out);

	if (status != MH_ERROR_NONE) {
		free(sample);
		return status;
	}

	fixture->opens++;
	*output = sample;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_sample(void *context, void *token, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct raw_sample *sample;

	(void)requested;
	fixture = context;
	sample = token;

	if (fixture->fail && sample->position == 1)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test partial read");

	/* Short reads force assembly independently of container/file size. */
	memcpy(output, sample->file.data + sample->position * 2, 2);
	sample->position++;
	*count = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
seek_sample(
	void *context, void *token, size_t frame, struct mh_error *error_out)
{
	struct raw_sample *sample;

	(void)context;
	sample = token;
	sample->position = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_sample(void *context, void *token)
{
	struct fixture *fixture;
	struct raw_sample *sample;

	fixture = context;
	sample = token;
	fixture->closes++;
	free(sample->file.data);
	free(sample);
}

static int
check_menu(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, struct fixture *fixture)
{
	struct mh_menu_sound_assets menu, before;
	struct mh_menu_horn_assets horns;
	struct mh_horn_catalog catalog;
	unsigned int opens;

	opens = fixture->opens;
	CHECK(mh_menu_sound_assets_load_from(
		decoder, source, "game", &menu, NULL) == MH_ERROR_NONE);
	CHECK(fixture->opens == opens + 1);
	CHECK(menu.bank.config.count == 2);
	CHECK(strcmp(menu.bank.config.samples[0].name, "Tone.WAV") == 0);
	CHECK(menu.bank.waves[0].pcm == menu.bank.waves[1].pcm &&
		menu.files[1].data == NULL);
	CHECK(mh_audio_pcm_sample(&menu.bank.waves[1], 0, 0) == .5);
	mh_menu_sound_assets_free(&menu);
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(), source,
		"game", &catalog, NULL) == MH_ERROR_NONE);
	opens = fixture->opens;
	CHECK(mh_menu_horn_assets_load_from(decoder, source, "game", &catalog,
		&horns, NULL) == MH_ERROR_NONE);
	CHECK(catalog.count == 2 && fixture->opens == opens + 1);
	CHECK(horns.bank.waves[0].pcm == horns.bank.waves[1].pcm &&
		horns.files[1].data == NULL);
	mh_menu_horn_assets_free(&horns);

	/* Failure after a partial decode preserves the caller's old output. */
	memset(&before, 0, sizeof(before));
	menu = before;
	fixture->fail = 1;
	CHECK(mh_menu_sound_assets_load_from(
		decoder, source, "game", &menu, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&menu, &before, sizeof(menu)) == 0);
	CHECK(fixture->opens == fixture->closes);
	fixture->fail = 0;

	return 0;
}

static int
check_race(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, struct fixture *fixture)
{
	struct mh_race_sound_assets race;
	struct mh_pause_sound_assets pause;
	unsigned int i, opens;

	opens = fixture->opens;
	CHECK(mh_race_sound_assets_load_from(mh_horn_fallback_source(), decoder,
		source, "game", NULL, &race, NULL) == MH_ERROR_NONE);
	CHECK(fixture->opens == opens + 13);
	CHECK(race.bank.absent_samples ==
		((1U << MH_SOUND_TURBO) | (1U << MH_SOUND_FINISH) |
			(1U << MH_SOUND_ELIMINATED) |
			(1U << MH_SOUND_MESSAGE)));
	CHECK(mh_audio_pcm_sample(&race.bank.waves[0], 0, 0) == .5);

	for (i = 1; i < MH_RACE_MAX_CARS; i++) {
		CHECK(race.bank.horns[i].wave.pcm ==
			race.bank.horns[0].wave.pcm);
		CHECK(race.horn_files[i].data == NULL);
	}

	mh_race_sound_assets_free(&race);
	CHECK(mh_pause_sound_assets_load_from(
		decoder, source, "game", &pause, NULL) == MH_ERROR_NONE);
	CHECK(pause.bank.absent_samples == 13);
	CHECK(mh_audio_pcm_sample(&pause.bank.waves[1], 0, 0) == .5);
	mh_pause_sound_assets_free(&pause);

	return 0;
}

static int
check_environment(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, struct fixture *fixture)
{
	struct mh_ambient_assets ambient;
	struct mh_surface_assets surfaces;
	unsigned int opens;

	opens = fixture->opens;
	CHECK(mh_ambient_assets_load_from(mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), decoder, source, "game",
		"env.esd", NULL, &ambient, NULL) == MH_ERROR_NONE);
	CHECK(ambient.bank.count == 2 && fixture->opens == opens + 1);
	CHECK(ambient.bank.waves[0].pcm == ambient.bank.waves[1].pcm &&
		ambient.files[1].data == NULL);
	CHECK(mh_audio_pcm_sample(&ambient.bank.waves[1], 0, 0) == .5);
	mh_ambient_assets_free(&ambient);
	CHECK(mh_surface_assets_load_from(mh_material_mat_srf_source(),
		mh_env_sounds_esd_source(), decoder, source, "game",
		"track.srf", "env.esd", &surfaces,
		NULL) == MH_ERROR_NONE);
	CHECK(surfaces.bank.count == MH_SURFACE_FIXED_SAMPLES);
	CHECK(mh_audio_pcm_sample(&surfaces.bank.waves[0], 0, 0) == .5);
	CHECK(surfaces.bank.crashes.samples[0].wave.pcm != NULL);
	mh_surface_assets_free(&surfaces);

	return 0;
}

static int
check_paths(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source)
{
	char path[128];

	CHECK(mh_audio_sample_path_from(decoder, source, "game",
		"sounds\\CARHORNS\\tone.WAV", path, sizeof(path),
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "game/Sounds/CarHorns/tone.sample") == 0);
	CHECK(mh_audio_sample_path_from(decoder, source, "game", "Sounds/enter",
		path, sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_sample_path_from(decoder, source, "game",
		"Sounds/error.wav", path, sizeof(path),
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_audio_sample_path_from(decoder, source, "game", "../enter.wav",
		path, sizeof(path), NULL) == MH_ERROR_INVALID);

	return 0;
}

int
main(void)
{
	static const unsigned char pcm[] = {0, 64, 0, 0, 0, 192, 0, 0};
	static const unsigned char menu[] =
		"SampleName Tone.WAV\nSampleEvent 1\n"
		"SampleName tone.wav\nSampleEvent 2\n";
	static const unsigned char horn[] =
		"Name Honker1\nSampleName CarHorns/tone.wav\n";
	static const unsigned char other_horn[] =
		"Name Other\nSampleName CarHorns/TONE.WAV\n";
	static const unsigned char env[] =
		"EnvSound tone.wav 1 1 1 1 100 0 0 0\n"
		"EnvSound TONE.WAV 1 1 1 1 100 1 0 0\n";
	static const unsigned char surfaces[] = "Materials 1\nMaterial road\n";
	static const unsigned char materials[] =
		"Materials 1\nMaterialName road\n"
		"SndContact slide1.wav 1 2 .5 1 0 0\n";
	static const struct mh_audio_decoder_ops ops = {
		inspect, open_sample, read_sample, seek_sample, close_sample};

#define SAMPLE(name) {"game/Sounds/" name ".sample", pcm, sizeof(pcm)}
#define TEXT(path, bytes) {"game/" path, bytes, sizeof(bytes) - 1}
	struct mh_resource_entry entries[] = {
		TEXT("Sounds/Menu/menusound.cfg", menu),
		TEXT("Sounds/CarHorns/honker1.chf", horn),
		TEXT("Sounds/CarHorns/other.chf", other_horn),
		TEXT("env.esd", env), TEXT("track.srf", surfaces),
		SAMPLE("Menu/tone"), SAMPLE("CarHorns/tone"), SAMPLE("tone"),
		SAMPLE("gason"), SAMPLE("gasrele"), SAMPLE("gasotl"),
		SAMPLE("gasotr"), SAMPLE("gasrtl"), SAMPLE("gasrtr"),
		SAMPLE("inneen"), SAMPLE("enemyeng"), SAMPLE("gear3"),
		SAMPLE("starten/!32"), SAMPLE("starten/go!"),
		SAMPLE("starten/checkpoint"), SAMPLE("enter"), SAMPLE("slide1"),
		SAMPLE("slidee1"), SAMPLE("slidee2"), SAMPLE("spin"),
		SAMPLE("barrel"), SAMPLE("crash2"), SAMPLE("majcrash"),
		SAMPLE("eburst1"), SAMPLE("eburst2"),
		/*
		 * A wrong-format resource must not mask a missing selected one.
		 */
		{"game/Sounds/error.wav", pcm, sizeof(pcm)},
		{"game/Data/Material.mat", NULL, 0}};

#undef SAMPLE
#undef TEXT
	struct mh_audio_decoder_provider decoder;
	const struct mh_resource_source *source;
	struct fixture fixture;
	unsigned char *table;
	size_t size, count;

	memset(&fixture, 0, sizeof(fixture));
	decoder.resolve = NULL;
	decoder.extension = "sample";
	decoder.ops = &ops;
	decoder.context = &fixture;
	CHECK(mh_tbl_encode_named(materials, sizeof(materials) - 1, NULL,
		&table, &size, NULL) == MH_ERROR_NONE);
	count = sizeof(entries) / sizeof(entries[0]);
	entries[count - 1].data = table;
	entries[count - 1].size = size;
	CHECK(mh_memory_resources_open(entries, count, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_paths(&decoder, source) == 0);
	CHECK(check_menu(&decoder, source, &fixture) == 0);
	CHECK(check_race(&decoder, source, &fixture) == 0);
	CHECK(check_environment(&decoder, source, &fixture) == 0);
	CHECK(fixture.opens == fixture.closes);
	mh_memory_resources_close(source);
	free(table);
	puts("All sound banks load alternate encoded samples through selected decoding");

	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_demo_horn.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/audio/horn.h"
#include "engine/audio/menu_sound_assets.h"
#include "engine/audio/race_sound_assets.h"
#include "interfaces/audio/horn_source.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo horn line %d: %s\n", __LINE__,   \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_preview(
	const struct mh_resource_source *source, const char *root, int silent)
{
	struct mh_horn_catalog catalog;
	struct mh_menu_horn_assets assets;
	unsigned int index;

	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(), source, root,
		&catalog, NULL) == MH_ERROR_NONE);
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_horn_assets_load_from(mh_audio_fallback_decoder(), source,
		root, &catalog, &assets, NULL) == MH_ERROR_NONE);

	if (silent) {
		CHECK(catalog.count == 1 && index == 0);
		CHECK(strcmp(catalog.horns[0].name, "None") == 0);
		CHECK(strcmp(catalog.horns[0].sample, "<None>") == 0);
		CHECK(assets.files[0].data == NULL &&
			assets.bank.waves[0].pcm == NULL);
	} else {
		CHECK(strcmp(catalog.horns[index].name, "Honker1") == 0);
		CHECK(assets.bank.waves[index].pcm != NULL);
	}

	mh_menu_horn_assets_free(&assets);

	return 0;
}

static int
check_preparation(void)
{
	static const char *paths[] = {"gason.wav", "gasrele.wav", "gasotl.wav",
		"gasotr.wav", "gasrtl.wav", "gasrtr.wav", "inneen.wav",
		"turbo.wav", "enemyeng.wav", "gear3.wav", "starten/!32.wav",
		"starten/go!.wav", "starten/checkpoint.wav", "finish2.wav",
		"eliminated1.wav", "menu/cursor1.wav"};

	static const unsigned char wave[46] = {'R', 'I', 'F', 'F', 38, 0, 0, 0,
		'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0, 'd', 'a', 't',
		'a', 2, 0, 0, 0, 128, 128};

	struct mh_resource_entry entries[17];
	char names[16][80];
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_demo_recipe recipe;
	struct mh_race_sound_assets assets;
	struct mh_file definition, unexpected;
	unsigned int i;

	memset(&recipe, 0, sizeof(recipe));
	recipe.track_layout = MH_DEMO_TRACK_SOFTWARE_FIRST;

	for (i = 0; i < 16; i++) {
		sprintf(names[i], "root/Sounds/%s", paths[i]);
		entries[i].path = names[i];
		entries[i].data = wave;
		entries[i].size = sizeof(wave);
	}

	CHECK(mh_memory_resources_open(entries, 16, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_horn_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/Sounds/CarHorns/None.chf", 1024,
		&definition, NULL) == MH_ERROR_NONE);
	mh_memory_resources_close(source);
	entries[16].path = "root/Sounds/CarHorns/None.chf";
	entries[16].data = definition.data;
	entries[16].size = definition.size;
	CHECK(mh_memory_resources_open(entries, 17, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_preview(source, "root", 1) == 0);
	CHECK(mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), source, "root", NULL,
		&assets, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_RACE_MAX_CARS; i++) {
		CHECK(assets.bank.horns[i].wave.pcm == NULL);
		CHECK(assets.horn_files[i].data == NULL);
		CHECK(strcmp(assets.horn_samples[i], "<None>") == 0);
	}

	mh_race_sound_assets_free(&assets);
	mh_memory_user_data_close(data);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_horn_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_EXISTS);
	CHECK(mh_user_data_read(data, entries[16].path, 1024, &unexpected,
		NULL) == MH_ERROR_NOT_FOUND);
	recipe.track_layout = MH_DEMO_TRACK_MPLAYER;
	CHECK(mh_demo_horn_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, entries[16].path, 1024, &unexpected,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);
	free(definition.data);

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 1)
		return check_preparation();

	if (argc == 3 &&
		(strcmp(argv[2], "silent") == 0 ||
			strcmp(argv[2], "supplied") == 0))
		return check_preview(mh_filesystem_resources(), argv[1],
			strcmp(argv[2], "silent") == 0);

	fprintf(stderr,
		"Usage: test-demo-horn [PREPARED-GAME silent|supplied]\n");

	return 1;
}

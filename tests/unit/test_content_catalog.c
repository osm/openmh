#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/settings/store.h"
#include "engine/assets/installed.h"
#include "engine/assets/tbl.h"
#include "engine/catalog/load.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "content catalog:%d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

#define TEST_CARS (MH_INSTALLED_CARS + 1)
#define TEST_TRACKS (MH_INSTALLED_TRACKS + 1)
#define TEST_ENTRIES (TEST_CARS + TEST_TRACKS)

static int
check_settings(const struct mh_resource_source *resources)
{
	static const char *const renderer_names[] = {"Test renderer"};
	struct mh_settings_names renderers;
	struct mh_settings defaults;
	struct mh_settings_store *store;
	const struct mh_user_data *data;
	unsigned long random;

	renderers.names = renderer_names;
	renderers.count = 1;
	random = 17;
	CHECK(mh_settings_init(&defaults, "Driver", 255, 127, 0, NULL) ==
		MH_ERROR_NONE);
	defaults.car = TEST_CARS - 1;
	defaults.track = TEST_TRACKS - 1;
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, data, &renderers,
		"assets", "settings.cfg", &defaults, &random, &store,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_catalog(store)->cars.count == TEST_CARS);
	CHECK(mh_settings_store_catalog(store)->tracks.count == TEST_TRACKS);
	CHECK(mh_settings_store_save(store, NULL) == MH_ERROR_NONE);
	mh_settings_store_close(store);
	defaults.car = defaults.track = 0;
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, data, &renderers,
		"assets", "settings.cfg", &defaults, &random, &store,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_values(store)->car == TEST_CARS - 1);
	CHECK(mh_settings_store_values(store)->track == TEST_TRACKS - 1);
	mh_settings_store_close(store);
	mh_memory_user_data_close(data);

	return 0;
}

static int
check_named_tracks(void)
{
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[3];
	struct mh_track_catalog tracks;
	unsigned char *bytes[3];
	size_t sizes[3];
	unsigned int i;
	static const char *const paths[] = {"assets/Game/Track1.trk",
		"assets/Game/z-coast.trk", "assets/Game/a-ridge.trk"};

	static const char *const named_paths[] = {
		"assets/Game/track64-coast.trk",
		"assets/Game/track100-coast.trk",
		"assets/Game/track999999999999999999999999999999-coast.trk"};

	static const char *const invalid_paths[] = {"assets/Game/Track65.trk",
		"assets/Game/Track0001.trk",
		"assets/Game/Track999999999999999999999999999999.trk"};

	static const char *const definitions[] = {
		"TrackName Original\nDivision 100\nEnvMapName a\nEnvMapName b\nEnvMapName c\n",
		"TrackName Coast\nDivision 100001\nEnvMapName a\nEnvMapName b\nEnvMapName c\n",
		"TrackName Ridge\nDivision 100002\nEnvMapName a\nEnvMapName b\nEnvMapName c\n"};

	memset(&tracks, 0, sizeof(tracks));

	for (i = 0; i < 3; i++) {
		CHECK(mh_tbl_encode_named((const unsigned char *)definitions[i],
			strlen(definitions[i]), NULL, &bytes[i],
			&sizes[i], NULL) == MH_ERROR_NONE);
		entries[i].path = paths[i];
		entries[i].data = bytes[i];
		entries[i].size = sizes[i];
	}

	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		resources, "assets", &tracks, NULL) == MH_ERROR_NONE);
	CHECK(tracks.count == 3 &&
		strcmp(tracks.entries[0].name, "Original") == 0);
	CHECK(strcmp(tracks.entries[1].name, "Ridge") == 0 &&
		tracks.entries[1].division == 100002);
	CHECK(strcmp(tracks.entries[2].name, "Coast") == 0 &&
		tracks.entries[2].division == 100001);
	mh_track_catalog_close(&tracks);
	mh_memory_resources_close(resources);

	/*
	 * Removing a preceding add-on retains the remaining course's identity.
	 */
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		resources, "assets", &tracks, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(tracks.entries[1].name, "Coast") == 0 &&
		tracks.entries[1].division == 100001);
	mh_track_catalog_close(&tracks);
	mh_memory_resources_close(resources);

	/* Numeric prefixes in named definitions are not stock track numbers. */
	for (i = 0; i < sizeof(named_paths) / sizeof(named_paths[0]); i++) {
		entries[1].path = named_paths[i];
		CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
			resources, "assets", &tracks,
			NULL) == MH_ERROR_NONE);
		CHECK(tracks.count == 2 &&
			strcmp(tracks.entries[1].name, "Coast") == 0);
		mh_track_catalog_close(&tracks);
		mh_memory_resources_close(resources);
	}

	for (i = 0; i < sizeof(invalid_paths) / sizeof(invalid_paths[0]); i++) {
		entries[1].path = invalid_paths[i];
		CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
			resources, "assets", &tracks,
			NULL) != MH_ERROR_NONE);
		CHECK(tracks.count == 0 && tracks.entries == NULL);
		mh_memory_resources_close(resources);
	}

	entries[1].path = paths[1];

	/* Two definitions cannot share a saved/network identity. */
	entries[2].data = entries[1].data;
	entries[2].size = entries[1].size;
	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		resources, "assets", &tracks, NULL) == MH_ERROR_INVALID);
	CHECK(tracks.count == 0 && tracks.entries == NULL);
	mh_memory_resources_close(resources);

	for (i = 0; i < 3; i++)
		free(bytes[i]);

	return 0;
}

int
main(void)
{
	static const char car_format[] =
		"CarName Car %u\nDivision 101\nfilename body\n"
		"Wheel1 fl wheel\nWheel2 fr wheel\nWheel3 br wheel\nWheel4 bl wheel\n"
		"PhyWheelFL -2 3 4\nPhyWheelFR 2 3 4\n"
		"PhyWheelBR 2 3 -5\nPhyWheelBL -2 3 -5\nPhySpringLength .5\n";
	static const char track_format[] =
		"TrackName Track%u\nDivision 101\n"
		"EnvMapName a\nEnvMapName b\nEnvMapName c\n";
	struct mh_resource_entry entries[TEST_ENTRIES + 1], swapped, alias[2];
	const struct mh_resource_source *resources;
	struct mh_car_catalog cars;
	struct mh_track_catalog tracks;
	unsigned char *data[TEST_ENTRIES];
	char paths[TEST_ENTRIES][64], text[512];
	size_t sizes[TEST_ENTRIES];
	unsigned int i, count;

	CHECK(check_named_tracks() == 0);
	memset(&cars, 0, sizeof(cars));
	memset(&tracks, 0, sizeof(tracks));

	for (i = 0; i < TEST_ENTRIES; i++) {
		if (i < TEST_CARS) {
			sprintf(paths[i], "assets/gAmE/Car%02u.CAR", i + 1);
			sprintf(text, car_format, i + 1);
		} else {
			sprintf(paths[i], "assets/gAmE/tRaCk%u.TRK",
				i - TEST_CARS + 1);
			sprintf(text, track_format, i - TEST_CARS + 1);
		}

		CHECK(mh_tbl_encode_named((const unsigned char *)text,
			strlen(text), NULL, &data[i], &sizes[i],
			NULL) == MH_ERROR_NONE);
		entries[i].path = paths[i];
		entries[i].data = data[i];
		entries[i].size = sizes[i];
	}

	entries[TEST_ENTRIES].path = "assets/Sounds/CarHorns/Signal.chf";
	entries[TEST_ENTRIES].data =
		(const unsigned char *)"Name Signal\nSampleName Horn.wav\n";
	entries[TEST_ENTRIES].size =
		strlen((const char *)entries[TEST_ENTRIES].data);

	/* Enumeration order must not change stable numbered identities. */
	swapped = entries[0];
	entries[0] = entries[TEST_CARS - 1];
	entries[TEST_CARS - 1] = swapped;
	CHECK(mh_memory_resources_open(entries, TEST_ENTRIES + 1, &resources,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_car_catalog_load_from(mh_catalog_fallback_source(), resources,
		"assets", &cars, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		resources, "assets", &tracks, NULL) == MH_ERROR_NONE);
	CHECK(check_settings(resources) == 0);
	CHECK(cars.count == TEST_CARS && tracks.count == TEST_TRACKS);
	CHECK(strcmp(cars.entries[0].name, "Car 1") == 0);
	CHECK(strcmp(cars.entries[10].name, "Car 11") == 0);
	CHECK(strcmp(tracks.entries[8].name, "Track9") == 0);
	CHECK(cars.entries[10].wheel_position[0][0] == -2);
	mh_memory_resources_close(resources);

	/* Definitions remain owned by the catalog after provider teardown. */
	CHECK(strcmp(tracks.entries[8].name, "Track9") == 0);
	mh_car_catalog_close(&cars);
	mh_track_catalog_close(&tracks);
	CHECK(cars.entries == NULL && cars.count == 0);

	/* Missing number one must not silently renumber the remaining cars. */
	CHECK(mh_memory_resources_open(entries, TEST_CARS - 1, &resources,
		NULL) == MH_ERROR_NONE);
	count = 123;
	CHECK(mh_car_catalog_count_from(mh_catalog_fallback_source(), resources,
		"assets", &count, NULL) == MH_ERROR_INVALID);
	CHECK(count == 123);
	CHECK(mh_car_catalog_load_from(mh_catalog_fallback_source(), resources,
		"assets", &cars, NULL) == MH_ERROR_INVALID &&
		cars.entries == NULL && cars.count == 0);
	mh_memory_resources_close(resources);

	alias[0] = swapped;
	alias[1] = swapped;
	alias[1].path = "assets/gAmE/car01.car";
	CHECK(mh_memory_resources_open(alias, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_car_catalog_count_from(mh_catalog_fallback_source(), resources,
		"assets", &count, NULL) == MH_ERROR_INVALID &&
		count == 123);
	mh_memory_resources_close(resources);

	for (i = 0; i < TEST_ENTRIES; i++)
		free(data[i]);

	puts("Content catalogs: extra cars/tracks, stable order, owned definitions and invalid identities passed");

	return 0;
}

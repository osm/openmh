#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/catalog_source.h"
#include "../support/horn_source.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/settings/store.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "catalog source:%d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_source(void)
{
	static const char *const renderer_names[] = {"Fixture"};
	struct mh_test_catalog_source decoded;
	struct mh_test_horn_source horn_data;
	struct mh_horn_source horns;
	struct mh_catalog_source source, partial;
	struct mh_car_catalog cars, failed_cars;
	struct mh_track_catalog tracks, failed_tracks;
	struct mh_car car, saved_car;
	struct mh_track track, saved_track;
	struct mh_settings_store *store;
	struct mh_settings defaults;
	struct mh_error error;
	enum mh_error_code status;
	struct mh_settings_names renderers;
	const struct mh_resource_source *resources;
	const struct mh_user_data *data;
	unsigned long random;
	unsigned int count, reads, i;

	memset(&decoded, 0, sizeof(decoded));
	memset(&horn_data, 0, sizeof(horn_data));
	horn_data.catalog.count = 1;
	strcpy(horn_data.catalog.horns[0].name, "Converted horn");
	strcpy(horn_data.catalog.horns[0].sample, "sound.converted");
	horn_data.catalog.horns[0].volume = horn_data.catalog.horns[0].pitch =
		1;
	mh_test_horn_source_init(&horn_data, &horns);
	memset(&cars, 0, sizeof(cars));
	memset(&tracks, 0, sizeof(tracks));
	memset(&failed_cars, 0, sizeof(failed_cars));
	memset(&failed_tracks, 0, sizeof(failed_tracks));
	decoded.cars.count = decoded.tracks.count = 2;
	decoded.cars.entries = calloc(2, sizeof(*decoded.cars.entries));
	decoded.tracks.entries = calloc(2, sizeof(*decoded.tracks.entries));
	CHECK(decoded.cars.entries != NULL && decoded.tracks.entries != NULL);

	for (i = 0; i < 2; i++) {
		sprintf(decoded.cars.entries[i].name, "Authored car %u", i);
		strcpy(decoded.cars.entries[i].body,
			"geometry/vehicle.converted");
		decoded.cars.entries[i].mass = 750 + i;
		decoded.cars.entries[i].has_division = 1;
		decoded.cars.entries[i].division = 101;
		sprintf(decoded.tracks.entries[i].name, "Authored track %u", i);
		strcpy(decoded.tracks.entries[i].world_file,
			"geometry/circuit.converted");
		decoded.tracks.entries[i].cd_track = i + 2;
		decoded.tracks.entries[i].has_division = 1;
		decoded.tracks.entries[i].division = 101 + i;
	}

	mh_test_catalog_source_init(&decoded, &source);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	count = 123;
	CHECK(mh_car_catalog_count_from(&source, resources, "art", &count,
		NULL) == MH_ERROR_NONE &&
		count == 2);
	decoded.cars.count = MH_CAR_CATALOG_LIMIT + 1;
	CHECK(mh_car_catalog_count_from(&source, resources, "art", &count,
		NULL) == MH_ERROR_RANGE &&
		count == 2);
	decoded.cars.count = 0;
	CHECK(mh_car_catalog_count_from(&source, resources, "art", &count,
		NULL) == MH_ERROR_RANGE &&
		count == 2);
	decoded.cars.count = 2;
	CHECK(mh_car_catalog_load_from(
		&source, resources, "art", &cars, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(&source, resources, "art", &tracks,
		NULL) == MH_ERROR_NONE);
	CHECK(cars.entries != decoded.cars.entries &&
		tracks.entries != decoded.tracks.entries);
	CHECK(strcmp(cars.entries[1].body, "geometry/vehicle.converted") == 0);
	CHECK(strcmp(tracks.entries[1].world_file,
		"geometry/circuit.converted") == 0);
	car = saved_car = cars.entries[0];
	track = saved_track = tracks.entries[0];
	reads = decoded.car_reads;
	CHECK(mh_car_config_load_from(&source, resources, "art", 0, &car,
		NULL) == MH_ERROR_INVALID);
	CHECK(decoded.car_reads == reads &&
		memcmp(&car, &saved_car, sizeof(car)) == 0);
	decoded.fail_car = decoded.fail_track = 2;
	CHECK(mh_car_config_load_from(&source, resources, "art", 2, &car,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_track_config_load_from(&source, resources, "art", 2, &track,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&car, &saved_car, sizeof(car)) == 0 &&
		memcmp(&track, &saved_track, sizeof(track)) == 0);
	CHECK(mh_car_catalog_load_from(&source, resources, "art", &failed_cars,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_track_catalog_load_from(&source, resources, "art",
		&failed_tracks, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(failed_cars.entries == NULL && failed_cars.count == 0 &&
		failed_tracks.entries == NULL && failed_tracks.count == 0);
	partial = source;
	partial.load_car = NULL;
	CHECK(mh_car_config_load_from(&partial, resources, "art", 1, &car,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_track_config_load_from(&partial, resources, "art", 1, &track,
		NULL) == MH_ERROR_NONE);
	decoded.fail_car = decoded.fail_track = 0;
	CHECK(mh_car_catalog_load_from(&source, resources, "art", &cars,
		NULL) == MH_ERROR_INVALID);

	renderers.names = renderer_names;
	renderers.count = 1;
	random = 17;
	CHECK(mh_settings_init(&defaults, "Driver", 255, 127, 0, NULL) ==
		MH_ERROR_NONE);
	defaults.car = defaults.track = 1;
	status = mh_settings_store_open_with_sources(&horns, &source, resources,
		data, &renderers, "art", "settings.cfg", &defaults, &random,
		&store, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	CHECK(status == MH_ERROR_NONE);
	CHECK(mh_settings_store_catalog(store)->cars.count == 2);
	CHECK(mh_settings_store_catalog(store)->tracks.count == 2);
	CHECK(decoded.car_counts > 1 && decoded.track_counts > 1);
	mh_memory_resources_close(resources);
	mh_test_catalog_source_free(&decoded);
	CHECK(horn_data.reads == 1);
	memset(&horn_data, 0, sizeof(horn_data));
	memset(&horns, 0, sizeof(horns));
	memset(&source, 0, sizeof(source));
	CHECK(cars.entries[1].mass == 751 && tracks.entries[1].cd_track == 3);
	CHECK(strcmp(mh_settings_store_catalog(store)->cars.names[1],
		"Authored car 1") == 0);
	CHECK(strcmp(mh_settings_store_catalog(store)->horns.names[0],
		"Converted horn") == 0);
	CHECK(mh_settings_store_save(store, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_reload(store, &random, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_values(store)->car == 1 &&
		mh_settings_store_values(store)->track == 1);
	mh_settings_store_close(store);
	mh_memory_user_data_close(data);
	mh_car_catalog_close(&cars);
	mh_track_catalog_close(&tracks);

	return 0;
}

int
main(void)
{
	CHECK(check_source() == 0);
	puts("Selected catalog: authored definitions, settings, stable IDs, owned values and failure cleanup passed");

	return 0;
}

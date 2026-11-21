#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/profile.h"
#include "engine/assets/tbl.h"
#include "engine/catalog/load.h"
#include "engine/catalog/profile.h"
#include "engine/catalog/profile_load.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/horn_source.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

#define LISTING_CAPACITY 4

struct resource_listing {
	unsigned int			count;
	enum mh_directory_action	action;
	char				names[LISTING_CAPACITY][32];
};

static enum mh_directory_action
collect_name(void *context, const char *name)
{
	struct resource_listing *listing;

	listing = context;

	if (listing->count >= LISTING_CAPACITY ||
		strlen(name) >= sizeof(listing->names[0]))
		return MH_DIRECTORY_FAIL;

	strcpy(listing->names[listing->count++], name);

	return listing->action;
}

static int
check_providers(void)
{
	static const unsigned char first[] = "first", second[] = "second";
	struct mh_resource_entry entries[] = {{"tree/Game/empty", NULL, 0},
		{"tree/Game/data", first, sizeof(first)},
		{"tree/Other/data", second, sizeof(second)}};

	struct mh_resource_entry other = {
		"tree/Game/data", second, sizeof(second)};

	const struct mh_resource_source *first_source, *second_source,
		*retained;
	struct mh_resource_source source;
	struct mh_resource_ops ops;
	struct mh_file file, copy;
	struct resource_listing listing;
	unsigned long size;
	char path[128];
	unsigned char range[3];
	int found;

	CHECK(mh_memory_resources_open(entries, 3, &first_source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_resources_open(&other, 1, &second_source, NULL) ==
		MH_ERROR_NONE);
	entries[1] = other;
	CHECK(mh_resource_source_read_at(first_source, "tree/Game/data", 1,
		range, sizeof(range), NULL) == MH_ERROR_NONE);
	CHECK(memcmp(range, "irs", sizeof(range)) == 0);
	CHECK(mh_resource_source_read_at(first_source, "tree/Game/data",
		sizeof(first), range, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_resource_source_read_at(first_source, "tree/Game/data",
		sizeof(first), range, 1, NULL) == MH_ERROR_RANGE);

	CHECK(mh_resource_source_read(first_source, "tree/Game/data", &file,
		NULL) == MH_ERROR_NONE);
	CHECK(file.size == sizeof(first) &&
		memcmp(file.data, first, file.size) == 0);
	CHECK(mh_resource_source_read(second_source, "tree/Game/data", &copy,
		NULL) == MH_ERROR_NONE);
	CHECK(copy.size == sizeof(second) &&
		memcmp(copy.data, second, copy.size) == 0);
	free(copy.data);
	CHECK(mh_resource_source_size(first_source, "tree/Game/data", &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == sizeof(first));
	CHECK(mh_resource_source_read(first_source, "tree/Game/empty", &copy,
		NULL) == MH_ERROR_NONE);
	CHECK(copy.data != NULL && copy.size == 0);
	free(copy.data);

	memset(&listing, 0, sizeof(listing));
	CHECK(mh_resource_source_list(first_source, "tree", collect_name,
		&listing, NULL) == MH_ERROR_NONE);
	CHECK(listing.count == 2 && strcmp(listing.names[0], "Game") == 0 &&
		strcmp(listing.names[1], "Other") == 0);
	listing.count = 0;
	listing.action = MH_DIRECTORY_STOP;
	CHECK(mh_resource_source_list(first_source, "tree", collect_name,
		&listing, NULL) == MH_ERROR_NONE);
	CHECK(listing.count == 1);
	listing.count = 0;
	listing.action = MH_DIRECTORY_FAIL;
	CHECK(mh_resource_source_list(first_source, "tree", collect_name,
		&listing, NULL) == MH_ERROR_IO);
	CHECK(listing.count == 1);
	CHECK(mh_resource_source_list(first_source, "absent", collect_name,
		&listing, NULL) == MH_ERROR_NOT_FOUND);

	CHECK(mh_asset_path_from(first_source, "tree", "gAmE\\DaTa", path,
		sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "tree/Game/data") == 0);
	CHECK(mh_asset_path_find_from(first_source, "tree", "Game/missing",
		path, sizeof(path), &found, NULL) == MH_ERROR_NONE &&
		!found);
	CHECK(mh_asset_path_find_from(first_source, "tree", "missing/../data",
		path, sizeof(path), &found, NULL) == MH_ERROR_INVALID);

	source = *first_source;
	ops = *first_source->ops;
	ops.read = NULL;
	CHECK(mh_resource_source_init(&source, &ops, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(source.ops == first_source->ops &&
		source.context == first_source->context);
	ops = *first_source->ops;
	ops.list = NULL;
	ops.read_at = NULL;
	CHECK(mh_resource_source_init(&source, &ops, first_source->context,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_resource_source_list(&source, "tree", collect_name, &listing,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_resource_source_read_at(&source, "tree/Game/data", 0, range, 1,
		NULL) == MH_ERROR_UNSUPPORTED);

	copy = file;
	CHECK(mh_resource_source_read_bounded(first_source, "tree/Game/data",
		sizeof(first) - 1, &copy, NULL) == MH_ERROR_RANGE);
	CHECK(copy.data == file.data && copy.size == file.size);
	CHECK(mh_resource_source_read_bounded(first_source, "tree/Game/data",
		sizeof(first), &copy, NULL) == MH_ERROR_NONE);
	CHECK(copy.size == sizeof(first) &&
		memcmp(copy.data, first, copy.size) == 0);
	free(copy.data);
	copy = file;
	size = 123;
	CHECK(mh_resource_source_read(first_source, "missing", &copy, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(copy.data == file.data && copy.size == file.size);
	CHECK(mh_resource_source_size(first_source, "missing", &size, NULL) ==
			MH_ERROR_NOT_FOUND &&
		size == 123);
	CHECK(mh_resource_source_read(NULL, "key", &copy, NULL) ==
		MH_ERROR_INVALID);

	retained = second_source;
	entries[0] = entries[1];
	CHECK(mh_memory_resources_open(entries, 3, &retained, NULL) ==
		MH_ERROR_INVALID);
	CHECK(retained == second_source);
	mh_memory_resources_close(first_source);
	CHECK(memcmp(file.data, first, file.size) == 0);
	free(file.data);
	CHECK(mh_resource_source_read(second_source, "tree/Game/data", &copy,
		NULL) == MH_ERROR_NONE);
	free(copy.data);
	mh_memory_resources_close(second_source);

	CHECK(mh_memory_resources_open(NULL, 0, &first_source, NULL) ==
		MH_ERROR_NONE);
	memset(&listing, 0, sizeof(listing));
	CHECK(mh_resource_source_list(first_source, "", collect_name, &listing,
		NULL) == MH_ERROR_NONE);
	CHECK(listing.count == 0);
	mh_memory_resources_close(first_source);

	return 0;
}

static int
check_configurations(void)
{
	static const unsigned char car_text[] =
		"CarName Memory Car\nfilename body\n"
		"Wheel1 fl wheel\nWheel2 fr wheel\nWheel3 br wheel\nWheel4 bl wheel\n"
		"PhyWheelFL -2 3 4\nPhyWheelFR 2 3 4\n"
		"PhyWheelBR 2 3 -5\nPhyWheelBL -2 3 -5\nPhySpringLength .5\n";
	static const unsigned char track_text[] =
		"TrackName MemoryTrack\nEnvMapName a\nEnvMapName b\nEnvMapName c\n";
	struct mh_resource_entry entries[3];
	const struct mh_resource_source *source;
	struct mh_car car;
	struct mh_track track;
	unsigned char *car_data, *track_data;
	size_t car_size, track_size;
	char path[128];

	CHECK(mh_tbl_encode_named(car_text, sizeof(car_text) - 1, NULL,
		&car_data, &car_size, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_encode_named(track_text, sizeof(track_text) - 1, NULL,
		&track_data, &track_size, NULL) == MH_ERROR_NONE);
	entries[0].path = "assets/Game/CAR01.car";
	entries[0].data = car_data;
	entries[0].size = car_size;
	entries[1].path = "assets/Game/track1.TRK";
	entries[1].data = track_data;
	entries[1].size = track_size;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_car_config_load_from(mh_catalog_fallback_source(), source,
		"assets", 1, &car, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(car.name, "Memory Car") == 0 && car.spring_length == .5);
	CHECK(mh_track_config_load_from(mh_catalog_fallback_source(), source,
		"assets", 1, &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.name, "MemoryTrack") == 0);
	mh_memory_resources_close(source);

	entries[2] = entries[0];
	entries[2].path = "assets/Game/car01.CAR";
	CHECK(mh_memory_resources_open(entries, 3, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_asset_path_from(source, "assets", "Game/car01.car", path,
		sizeof(path), NULL) == MH_ERROR_INVALID);
	mh_memory_resources_close(source);
	free(car_data);
	free(track_data);

	return 0;
}

static int
check_catalogs(void)
{
	static const unsigned char horn[] =
		"Name Signal\nSampleName Horn.wav\n";
	static const unsigned char driver[] =
		"PlayerName Driver\nPlayerNick D\nPlayerPicture driver.tga\nCarHorn Signal\n";
	struct mh_resource_entry entries[3];
	const struct mh_resource_source *source;
	struct mh_horn_catalog horns;
	struct mh_profile_catalog *profiles, *before;
	unsigned char *data;
	size_t size;
	unsigned long random, expected;
	struct mh_profile profile;

	CHECK(mh_tbl_encode_named(driver, sizeof(driver) - 1, NULL, &data,
		&size, NULL) == MH_ERROR_NONE);
	entries[0].path = "assets/Sounds/CarHorns/test.CHF";
	entries[0].data = horn;
	entries[0].size = sizeof(horn) - 1;
	entries[1].path = "assets/League/Profiles/driver.ADP";
	entries[1].data = data;
	entries[1].size = size;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(), source,
		"assets", &horns, NULL) == MH_ERROR_NONE);
	CHECK(horns.count == 1 && strcmp(horns.horns[0].name, "Signal") == 0);
	profiles = malloc(sizeof(*profiles));
	before = malloc(sizeof(*before));
	CHECK(profiles != NULL && before != NULL);
	random = expected = 17;
	CHECK(mh_profile_catalog_advance_rng(1, &expected, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_profile_adp_decode(driver, sizeof(driver) - 1, &profile,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_profile_initialize(&profile, &horns, &expected, &profile,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_profile_catalog_load_from(mh_profile_fallback_source(), source,
		"assets", &horns, &random, profiles,
		NULL) == MH_ERROR_NONE);
	CHECK(profiles->count == 1 && random == expected);
	CHECK(strcmp(profiles->files[0], "driver.ADP") == 0);
	CHECK(memcmp(&profile, &profiles->profiles[0], sizeof(profile)) == 0);
	mh_memory_resources_close(source);

	/*
	 * A later malformed profile must not publish a partial catalog or RNG.
	 */
	entries[2].path = "assets/League/Profiles/zbad.adp";
	entries[2].data = (const unsigned char *)"bad";
	entries[2].size = 3;
	CHECK(mh_memory_resources_open(entries, 3, &source, NULL) ==
		MH_ERROR_NONE);
	*before = *profiles;
	expected = random;
	CHECK(mh_profile_catalog_load_from(mh_profile_fallback_source(), source,
		"assets", &horns, &random, profiles,
		NULL) != MH_ERROR_NONE);
	CHECK(random == expected &&
		memcmp(before, profiles, sizeof(*before)) == 0);
	mh_memory_resources_close(source);
	free(profiles);
	free(before);
	free(data);

	return 0;
}

int
main(void)
{
	CHECK(check_providers() == 0);
	CHECK(check_configurations() == 0);
	CHECK(check_catalogs() == 0);
	puts("Independent prepared resources, enumeration and memory configuration loading passed");

	return 0;
}

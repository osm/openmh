#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/profile_source.h"
#include "backends/storage/memory_resource.h"
#include "engine/catalog/profile_load.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "profile source:%d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_test_profile_source *data;
	struct mh_profile_source source, unavailable;
	struct mh_profile_catalog *catalog, *saved;
	struct mh_profile single, before;
	struct mh_horn_catalog horns;
	const struct mh_resource_source *resources;
	unsigned long random;
	unsigned int i, reads;

	data = calloc(1, sizeof(*data));
	catalog = malloc(sizeof(*catalog));
	saved = malloc(sizeof(*saved));
	CHECK(data != NULL && catalog != NULL && saved != NULL);
	data->catalog.count = 2;

	for (i = 0; i < data->catalog.count; i++) {
		sprintf(data->catalog.files[i], "driver%u.converted", i);
		sprintf(data->catalog.profiles[i].name, "Driver %u", i);
		sprintf(data->catalog.profiles[i].nick, "D%u", i);
		strcpy(data->catalog.profiles[i].picture, "portrait.converted");
		data->catalog.profiles[i].aggressiveness = 0.5;
		data->catalog.profiles[i].eagerness = 0.5;
	}

	data->catalog.profiles[1].league_fields = MH_PROFILE_HORN_FIELD;
	strcpy(data->catalog.profiles[1].horn, "Horn3");
	memset(&horns, 0, sizeof(horns));
	horns.count = 2;
	strcpy(horns.horns[0].name, "Signal");
	strcpy(horns.horns[1].name, "Bell");
	mh_test_profile_source_init(data, &source);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_profile_load_from(&source, resources, "assets",
		"driver1.converted", &single, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(single.horn, "Horn3") == 0);
	random = 1;
	CHECK(mh_profile_catalog_load_from(&source, resources, "assets", &horns,
		&random, catalog, NULL) == MH_ERROR_NONE);
	CHECK(random == 1045529057UL && catalog->count == 2);
	CHECK(strcmp(catalog->profiles[1].horn, "Bell") == 0);
	CHECK(strcmp(data->catalog.profiles[1].horn, "Horn3") == 0);
	*saved = *catalog;
	before = single;
	data->fail = 1;
	random = 7;
	CHECK(mh_profile_catalog_load_from(&source, resources, "assets", &horns,
		&random, catalog, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(random == 7 && memcmp(catalog, saved, sizeof(*catalog)) == 0);
	CHECK(mh_profile_load_from(&source, resources, "assets", "missing",
		&single, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&single, &before, sizeof(single)) == 0);
	reads = data->reads;
	CHECK(mh_profile_load_from(&source, resources, NULL,
		"driver0.converted", &single, NULL) == MH_ERROR_INVALID);
	CHECK(data->reads == reads);
	unavailable = source;
	unavailable.load = NULL;
	CHECK(mh_profile_load_from(&unavailable, resources, "assets",
		"driver0.converted", &single,
		NULL) == MH_ERROR_UNSUPPORTED);
	data->fail = 0;
	strcpy(data->catalog.profiles[1].nick, "d0");
	CHECK(mh_profile_catalog_load_from(&source, resources, "assets", &horns,
		&random, catalog, NULL) == MH_ERROR_INVALID);
	CHECK(random == 7 && memcmp(catalog, saved, sizeof(*catalog)) == 0);
	data->catalog.count = MH_PROFILE_CATALOG_LIMIT + 1;
	CHECK(mh_profile_catalog_load_from(&source, resources, "assets", &horns,
		&random, catalog, NULL) == MH_ERROR_RANGE);
	CHECK(random == 7 && memcmp(catalog, saved, sizeof(*catalog)) == 0);
	mh_memory_resources_close(resources);
	free(data);
	memset(&source, 0, sizeof(source));
	CHECK(strcmp(catalog->files[0], "driver0.converted") == 0);
	CHECK(strcmp(catalog->profiles[1].picture, "portrait.converted") == 0);
	CHECK(strcmp(single.nick, "D1") == 0);
	free(saved);
	free(catalog);
	puts("Selected driver source: authored profiles, portable RNG, owned values and atomic failures passed");

	return 0;
}

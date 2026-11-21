#include <stdio.h>
#include <string.h>

#include "../support/horn_source.h"
#include "backends/storage/memory_resource.h"
#include "engine/audio/horn.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "horn source:%d: %s\n", __LINE__, #e); \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_test_horn_source decoded;
	struct mh_horn_source source, unavailable;
	struct mh_horn_catalog catalog, saved;
	const struct mh_resource_source *resources;
	unsigned int reads, index;

	memset(&decoded, 0, sizeof(decoded));
	decoded.catalog.count = 2;
	strcpy(decoded.catalog.horns[0].name, "Converted bell");
	strcpy(decoded.catalog.horns[0].sample, "effects/bell.converted");
	decoded.catalog.horns[0].volume = 0.5;
	decoded.catalog.horns[0].pitch = 1;
	strcpy(decoded.catalog.horns[1].name, "Quiet");
	strcpy(decoded.catalog.horns[1].sample, "<None>");
	decoded.catalog.horns[1].volume = decoded.catalog.horns[1].pitch = 1;
	mh_test_horn_source_init(&decoded, &source);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_horn_catalog_load_from(&source, resources, "assets", &catalog,
		NULL) == MH_ERROR_NONE);
	saved = catalog;
	decoded.fail = 1;
	CHECK(mh_horn_catalog_load_from(&source, resources, "assets", &catalog,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&catalog, &saved, sizeof(catalog)) == 0);
	reads = decoded.reads;
	CHECK(mh_horn_catalog_load_from(&source, resources, NULL, &catalog,
		NULL) == MH_ERROR_INVALID);
	CHECK(decoded.reads == reads);
	unavailable = source;
	unavailable.load = NULL;
	CHECK(mh_horn_catalog_load_from(&unavailable, resources, "assets",
		&catalog, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&catalog, &saved, sizeof(catalog)) == 0);
	mh_memory_resources_close(resources);
	memset(&decoded, 0, sizeof(decoded));
	memset(&source, 0, sizeof(source));
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_NONE);
	CHECK(index == 0 && catalog.horns[0].volume == 0.5);
	CHECK(strcmp(catalog.horns[0].sample, "effects/bell.converted") == 0);
	CHECK(mh_horn_find(&catalog, "quiet", &index, NULL) == MH_ERROR_NONE);
	CHECK(index == 1 && strcmp(catalog.horns[index].sample, "<None>") == 0);
	puts("Selected horn source: authored catalog, stable ordering, owned values and failures passed");

	return 0;
}

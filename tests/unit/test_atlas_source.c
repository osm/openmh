#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/atlas_source.h"
#include "engine/image/atlas_map.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned char key[2] = {0, 0};
	static const unsigned char bad_key[2] = {1, 0};
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *resources;
	struct mh_test_atlas_source fixture;
	struct mh_atlas_source source, incomplete;
	struct mh_atlas_map output, saved;
	struct mh_atlas_map_entry *entry;
	struct mh_error error;

	memset(&fixture, 0, sizeof(fixture));
	memset(&output, 0, sizeof(output));
	memset(&incomplete, 0, sizeof(incomplete));
	fixture.count = 1;
	fixture.maps[0].count = fixture.maps[0].pages = 1;
	fixture.maps[0].width = fixture.maps[0].height = 8;
	fixture.maps[0].scale[0] = fixture.maps[0].scale[1] = 1;
	fixture.maps[0].entries = calloc(1, sizeof(*entry));
	CHECK(fixture.maps[0].entries != NULL);
	entry = fixture.maps[0].entries;
	strcpy(entry->name, "paint.iff");
	entry->source_width = entry->scaled_width = 8;
	entry->source_height = entry->scaled_height = 8;
	entries[0].path = "memory/Track/map.fixtureatlas";
	entries[0].data = key;
	entries[0].size = sizeof(key);
	entries[1].path = "memory/Track/bad.fixtureatlas";
	entries[1].data = bad_key;
	entries[1].size = sizeof(bad_key);
	mh_test_atlas_source_init(&fixture, &source);
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_atlas_map_load_from(&source, resources, "memory",
		"TRACK\\MAP.BIN", &output, &error) == MH_ERROR_NONE);
	CHECK(output.count == 1 && output.entries != entry &&
		error.message == NULL);
	CHECK(memcmp(output.entries, entry, sizeof(*entry)) == 0);
	saved = output;
	CHECK(mh_atlas_map_load_from(&source, resources, "memory",
		"Track/bad.bin", &output, &error) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &output, sizeof(output)) == 0);
	CHECK(mh_atlas_map_load_from(&source, resources, "memory",
		"Track/missing.bin", &output,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_atlas_map_load_from(NULL, resources, "memory", "Track/map.bin",
		&output, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_atlas_map_load_from(&incomplete, resources, "memory",
		"Track/map.bin", &output, NULL) == MH_ERROR_INVALID);
	CHECK(mh_atlas_map_load_from(&source, resources, "memory",
		"Track/map.bin", NULL, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &output, sizeof(output)) == 0 &&
		fixture.reads == 1);
	mh_memory_resources_close(resources);
	mh_test_atlas_source_free(&fixture);
	CHECK(strcmp(output.entries[0].name, "paint.iff") == 0);
	CHECK(output.entries[0].scaled_width == 8);
	mh_atlas_map_free(&output);
	puts("Atlas source: renamed resources, owned decoded output and failure atomicity passed");

	return 0;
}

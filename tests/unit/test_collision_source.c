#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/collision_fixture.h"
#include "../support/collision_source.h"
#include "engine/collision/data.h"
#include "engine/collision/load.h"
#include "engine/simulation/collision.h"

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
	struct mh_test_collision_source source;
	struct mh_collision_source decoder;
	struct mh_resource_entry entry;
	const struct mh_resource_source *memory;
	struct mh_collision collision, saved;
	struct mh_contact hit;
	struct mh_error error;
	unsigned char *bytes;
	char *path;
	double start[3] = {0, 3, 0}, end[3] = {0, -1, 0};
	unsigned char invalid[2] = {255, 0};

	memset(&source, 0, sizeof(source));
	memset(&collision, 0, sizeof(collision));
	mh_test_collision_source_init(&source, &decoder);
	bytes = malloc(FLOOR_FIXTURE_SIZE);
	path = malloc(sizeof("memory/Tracks/Road.COL"));
	CHECK(bytes != NULL && path != NULL);
	strcpy(path, "memory/Tracks/Road.COL");
	entry.path = path;
	entry.data = bytes;
	entry.size = collision_floor_fixture(bytes, 1);
	CHECK(mh_test_collision_convert(&source, &entry, &error) ==
		MH_ERROR_NONE);
	CHECK(entry.size == 1 &&
		strcmp(entry.path, "memory/Tracks/Road.fixture") == 0);
	CHECK(mh_memory_resources_open(&entry, 1, &memory, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_collision_load_from(&decoder, memory, "memory",
		"tracks\\road.col", &collision, &error) == MH_ERROR_NONE);
	CHECK(source.reads == 1 && collision.polygon_count == 2);
	saved = collision;
	CHECK(mh_collision_load_from(&decoder, memory, "memory", "missing.col",
		&collision, &error) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&collision, &saved, sizeof(saved)) == 0);
	CHECK(mh_collision_source_decode(&decoder, invalid, sizeof(invalid),
		&collision, &error) == MH_ERROR_INVALID);
	CHECK(mh_collision_source_decode(&decoder, invalid, 1, &collision,
		&error) == MH_ERROR_INVALID);
	CHECK(memcmp(&collision, &saved, sizeof(saved)) == 0);
	CHECK(mh_collision_source_decode(NULL, invalid, 1, &collision,
		&error) == MH_ERROR_UNSUPPORTED);
	decoder.extension = "../bad";
	CHECK(mh_collision_load_from(&decoder, memory, "memory",
		"Tracks/Road.COL", &collision,
		&error) == MH_ERROR_INVALID);
	CHECK(source.reads == 1);
	CHECK(memcmp(&collision, &saved, sizeof(saved)) == 0);

	mh_memory_resources_close(memory);
	free((void *)entry.path);
	free((void *)entry.data);
	mh_test_collision_source_free(&source);
	memset(&decoder, 0, sizeof(decoder));
	CHECK(mh_collision_cast(&collision, start, end, &hit, &error) ==
		MH_ERROR_NONE);
	CHECK(hit.hit && hit.polygon == 1 && hit.material == 2 &&
		hit.fraction == .25);
	mh_collision_free(&collision);
	puts("Alternate collision resources, selected decoding, lifetime and actual road queries passed");

	return 0;
}

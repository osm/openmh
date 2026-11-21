#include <stdio.h>
#include <string.h>

#include "../support/surface_source.h"
#include "backends/storage/memory_resource.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "surface source:%d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_test_surface_source decoded;
	struct mh_material_source materials, unavailable;
	struct mh_env_sound_source environment;
	struct mh_materials table, saved_table;
	struct mh_env_sounds sounds, saved_sounds;
	const struct mh_resource_source *resources;
	unsigned int reads;

	memset(&decoded, 0, sizeof(decoded));
	decoded.materials.count = decoded.materials.surface_count = 1;
	strcpy(decoded.materials.entries[0].name, "authored road");
	decoded.materials.entries[0].z_roll = 0.75;
	decoded.materials.entries[0].sounds[MH_MATERIAL_SLIDE].volume = 0.5;
	decoded.materials.entries[0].sounds[MH_MATERIAL_SLIDE].pitch = 1;
	strcpy(decoded.materials.entries[0].sounds[MH_MATERIAL_SLIDE].sample,
		"road.converted");
	decoded.environment.count = 2;
	decoded.environment.sounds[0].volume = 0.5;
	decoded.environment.sounds[0].pitch = 1;
	decoded.environment.sounds[1].volume = 0.5;
	decoded.environment.sounds[1].pitch = 1;
	decoded.environment.sounds[0].kind = MH_ENV_FIXED;
	decoded.environment.sounds[0].position[0] = 12;
	decoded.environment.sounds[0].near_distance = 5;
	decoded.environment.sounds[0].far_distance = 100;
	strcpy(decoded.environment.sounds[0].sample, "ambience.converted");
	decoded.environment.sounds[1].kind = MH_ENV_COLLISION;
	decoded.environment.sounds[1].object_class = 3;
	strcpy(decoded.environment.sounds[1].sample, "contact.converted");
	mh_test_surface_sources_init(&decoded, &materials, &environment);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_materials_load_from(&materials, resources, "assets",
		MH_TEST_MATERIAL_REFERENCE, &table,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_env_sounds_load_from(&environment, resources, "assets",
		MH_TEST_ENVIRONMENT_REFERENCE, &sounds,
		NULL) == MH_ERROR_NONE);
	saved_table = table;
	saved_sounds = sounds;
	CHECK(mh_materials_load_from(&materials, resources, "assets", "missing",
		&table, NULL) == MH_ERROR_NOT_FOUND);
	decoded.fail = 1;
	CHECK(mh_env_sounds_load_from(&environment, resources, "assets",
		MH_TEST_ENVIRONMENT_REFERENCE, &sounds,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&table, &saved_table, sizeof(table)) == 0);
	CHECK(memcmp(&sounds, &saved_sounds, sizeof(sounds)) == 0);
	reads = decoded.material_reads;
	CHECK(mh_materials_load_from(&materials, resources, NULL,
		MH_TEST_MATERIAL_REFERENCE, &table,
		NULL) == MH_ERROR_INVALID);
	CHECK(decoded.material_reads == reads);
	unavailable = materials;
	unavailable.load = NULL;
	CHECK(mh_materials_load_from(&unavailable, resources, "assets",
		MH_TEST_MATERIAL_REFERENCE, &table,
		NULL) == MH_ERROR_UNSUPPORTED);
	mh_memory_resources_close(resources);
	memset(&decoded, 0, sizeof(decoded));
	memset(&materials, 0, sizeof(materials));
	memset(&environment, 0, sizeof(environment));
	CHECK(table.entries[0].z_roll == 0.75 && table.surface_count == 1);
	CHECK(strcmp(table.entries[0].sounds[MH_MATERIAL_SLIDE].sample,
		"road.converted") == 0);
	CHECK(sounds.sounds[0].position[0] == 12 &&
		sounds.sounds[1].kind == MH_ENV_COLLISION &&
		sounds.sounds[1].object_class == 3);
	puts("Selected material/environment sources: authored values, logical references, independent ownership and failures passed");

	return 0;
}

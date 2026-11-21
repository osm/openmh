#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/retro_source.h"
#include "backends/storage/memory_resource.h"
#include "engine/render/scene.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "retro source:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_test_retro_source *data;
	struct mh_retro_assets *decoded, *saved;
	struct mh_retro_source source, missing;
	struct mh_race_scene *scene;
	const struct mh_resource_source *resources;
	unsigned int reads;

	data = calloc(1, sizeof(*data));
	decoded = malloc(sizeof(*decoded));
	saved = malloc(sizeof(*saved));
	scene = calloc(1, sizeof(*scene));
	CHECK(data != NULL && decoded != NULL && saved != NULL &&
		scene != NULL);
	strcpy((char *)data->assets.text[0], "Authored retro story");
	data->assets.palette[0][0] = 127;
	data->assets.motion[2][MH_RETRO_ROTATION_RATE][1] = 0.25f;
	mh_test_retro_source_init(data, &source);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_retro_load_from(&source, resources, "memory",
		"effect.converted", decoded, NULL) == MH_ERROR_NONE);
	*saved = *decoded;
	data->fail = 1;
	CHECK(mh_retro_load_from(&source, resources, "memory",
		"effect.converted", decoded, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(saved, decoded, sizeof(*saved)) == 0);
	CHECK(mh_race_scene_load_retro_from(&source, resources, "memory", scene,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(scene->assets.retro == NULL);
	missing = source;
	missing.load = NULL;
	CHECK(mh_retro_load_from(&missing, resources, "memory",
		"effect.converted", decoded,
		NULL) == MH_ERROR_UNSUPPORTED);
	reads = data->reads;
	CHECK(mh_retro_load_from(&source, resources, NULL, "effect.converted",
		decoded, NULL) == MH_ERROR_INVALID);
	CHECK(data->reads == reads);
	data->fail = 0;
	CHECK(mh_race_scene_load_retro_from(&source, resources, "memory", scene,
		NULL) == MH_ERROR_NONE);
	reads = data->reads;
	CHECK(mh_race_scene_load_retro_from(&source, resources, "memory", scene,
		NULL) == MH_ERROR_NONE);
	CHECK(data->reads == reads);
	CHECK(memcmp(scene->assets.retro, saved, sizeof(*saved)) == 0);
	mh_memory_resources_close(resources);
	free(data);
	memset(&source, 0, sizeof(source));
	CHECK(strcmp((const char *)scene->assets.retro->text[0],
		"Authored retro story") == 0);
	CHECK(scene->assets.retro->motion[2][MH_RETRO_ROTATION_RATE][1] ==
		0.25f);
	CHECK(decoded->palette[0][0] == 127);
	free(scene->assets.retro);
	free(scene);
	free(saved);
	free(decoded);
	puts("Selected retro source: authored values, lazy scene ownership and atomic failures passed");

	return 0;
}

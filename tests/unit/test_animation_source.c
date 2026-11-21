#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/animation_source.h"
#include "engine/animation/scene.h"

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
	static const unsigned char scene_key[2] = {0, 0};
	static const unsigned char motion_key[2] = {1, 0};
	static const struct mh_resource_entry entries[] = {
		{"memory/Scenery.fixtureanim", scene_key, sizeof(scene_key)},
		{"memory/Start.fixtureanim", motion_key, sizeof(motion_key)}};

	struct mh_test_animation_source source;
	struct mh_animation_source provider, unavailable;
	const struct mh_resource_source *resources;
	struct mh_scene scene, before;
	struct mh_scene_object motion, sample;
	struct mh_scene_key *keys;
	struct mh_error error;
	size_t i;

	memset(&source, 0, sizeof(source));
	memset(&scene, 0, sizeof(scene));
	memset(&motion, 0, sizeof(motion));
	memset(&unavailable, 0, sizeof(unavailable));
	mh_test_animation_source_init(&source, &provider);
	source.count = 2;
	source.motion[1] = 1;

	/* Authored values reach the consumers without any LWS/MOT input. */
	for (i = 0; i < source.count; i++) {
		source.scenes[i].objects =
			calloc(1, sizeof(*source.scenes[i].objects));
		CHECK(source.scenes[i].objects != NULL);
		source.scenes[i].object_count = 1;
		source.scenes[i].key_count = 2;
		source.scenes[i].frames_per_second = 30;
		source.scenes[i].objects[0].keys = calloc(2, sizeof(*keys));
		keys = source.scenes[i].objects[0].keys;
		CHECK(keys != NULL);
		source.scenes[i].objects[0].key_count = 2;
		source.scenes[i].objects[0].end_behavior = MH_SCENE_HOLD;
		keys[1].time = 2;
		keys[1].linear = 1;
		keys[1].value[0] = 20;
	}

	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_scene_load_from(&provider, resources, "memory", "scenery.lws",
		&scene, &error) == MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(scene.objects != source.scenes[0].objects);
	CHECK(scene.objects[0].keys != source.scenes[0].objects[0].keys);
	CHECK(mh_motion_load_from(&provider, resources, "memory", "START.mot",
		&motion, NULL) == MH_ERROR_NONE);
	CHECK(motion.keys != source.scenes[1].objects[0].keys);
	before = scene;
	CHECK(mh_scene_load_from(&provider, resources, "memory", "missing.lws",
		&scene, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &scene, sizeof(scene)) == 0);
	CHECK(mh_scene_load_from(&provider, resources, "memory", "Start.mot",
		&scene, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&before, &scene, sizeof(scene)) == 0);
	CHECK(mh_scene_load_from(NULL, resources, "memory", "scenery.lws",
		&scene, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_motion_load_from(&unavailable, resources, "memory",
		"START.mot", &motion, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(source.scene_reads == 1 && source.motion_reads == 1);
	mh_memory_resources_close(resources);
	mh_test_animation_source_free(&source);

	CHECK(mh_scene_sample(&scene.objects[0], 1, &sample, NULL) ==
		MH_ERROR_NONE);
	CHECK(sample.position[0] == 10);
	CHECK(mh_scene_sample(&motion, 1, &sample, NULL) == MH_ERROR_NONE);
	CHECK(sample.position[0] == 10);
	mh_scene_free(&scene);
	free(motion.keys);
	puts("Selected animation: renamed assets, authored values, independent ownership and failures pass");

	return 0;
}

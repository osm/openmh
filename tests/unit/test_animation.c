#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/animation/scene.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/* Authored decoded keys: this target links no original asset readers. */
static void
check_motion(void)
{
	struct mh_scene_key keys[2];
	struct mh_scene_object object, sample, before;
	double values[9];
	unsigned int channel;

	memset(keys, 0, sizeof(keys));
	memset(&object, 0, sizeof(object));
	memset(&sample, 0, sizeof(sample));
	keys[1].time = 2;
	keys[1].linear = 1;
	keys[1].value[0] = 10;
	keys[1].value[3] = 180;
	keys[1].value[6] = 1;
	object.keys = keys;
	object.key_count = 2;
	object.end_behavior = MH_SCENE_REPEAT;

	CHECK(mh_scene_channels(&object, 3, values, NULL) == MH_ERROR_NONE);

	for (channel = 0; channel < 9; channel++)
		CHECK(values[channel] == keys[1].value[channel] / 2);

	CHECK(mh_scene_sample(&object, 1, &sample, NULL) == MH_ERROR_NONE);
	CHECK(sample.keys == keys && sample.key_count == 2);
	CHECK(sample.position[0] == 5 && sample.color[0] == 0.5);
	CHECK(fabs(sample.rotation[0]) < 1e-12);
	CHECK(fabs(sample.rotation[6] - 1) < 1e-12);
	CHECK(fabs(sample.rotation[2] + 1) < 1e-12);
	CHECK(object.position[0] == 0);
	before = sample;
	CHECK(mh_scene_sample(&object, -1, &sample, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&before, &sample, sizeof(sample)) == 0);
	object.end_behavior = MH_SCENE_HOLD;
	CHECK(mh_scene_channels(&object, 3, values, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(values, keys[1].value, sizeof(values)) == 0);
}

static void
check_owned_scene(void)
{
	struct mh_scene scene;

	memset(&scene, 0, sizeof(scene));
	scene.objects =
		(struct mh_scene_object *)calloc(1, sizeof(*scene.objects));
	CHECK(scene.objects != NULL);

	if (scene.objects == NULL)
		return;

	scene.object_count = 1;
	scene.objects[0].keys = (struct mh_scene_key *)calloc(
		1, sizeof(*scene.objects[0].keys));
	CHECK(scene.objects[0].keys != NULL);
	mh_scene_free(&scene);
	CHECK(scene.objects == NULL && scene.object_count == 0);
	mh_scene_free(&scene);
}

int
main(void)
{
	check_motion();
	check_owned_scene();

	if (failures != 0)
		return 1;

	puts("Decoded animation: sampling, borrowed keys, failure preservation and cleanup pass");

	return 0;
}

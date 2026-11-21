#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/animation/scene.h"
#include "engine/assets/scene.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static const char valid[] =
	"LWSC\r\n1\nFramesPerSecond 24\n"
	"LoadObject S:\\old\\dir\\Sign.lwo\nShowObject 7 7\nObjectMotion (unnamed)\n"
	"9\n2\n10 -2 3 90 0 0 0 0.5 1\n0 0 0 0 0\n"
	"20 -2 3 180 0 0 1 1 1\n24 1 0 0 0\nEndBehavior 2\nObjEdgeColor 11 8 0\nPolygonSize 0.2\nUnseenByRays 0\nShadowOptions 0\n"
	"LoadObject /old/prop.lwo\nObjectMotion (unnamed)\n9\n1\n"
	"0 0 0 0 90 0 1 1 1\n0 0 0 0 0\nEndBehavior 1\nShadowOptions 0";

static void
check_rejected(const char *text, struct mh_scene *scene)
{
	struct mh_scene before;

	before = *scene;
	CHECK(mh_scene_lws_parse((const unsigned char *)text, strlen(text),
		scene, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(scene, &before, sizeof(before)) == 0);
}

static void
check_decode(void)
{
	struct mh_scene scene, saved;
	unsigned char *wrapped;
	size_t size;
	unsigned int mode;
	enum mh_error_code status;

	status = mh_tbl_encode_named((const unsigned char *)valid,
		sizeof(valid) - 1, NULL, &wrapped, &size, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	for (mode = 0; mode < 2; mode++) {
		status = mh_scene_lws_decode(
			mode ? wrapped : (const unsigned char *)valid,
			mode ? size : sizeof(valid) - 1, &scene, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			CHECK(scene.object_count == 2 && scene.key_count == 3 &&
				scene.frames_per_second == 24);
			CHECK(strcmp(scene.objects[0].model, "sign.myo") == 0);
			CHECK(scene.objects[0].keys[1].value[0] == 20);
			mh_scene_free(&scene);
		}
	}

	memset(&saved, 0x5a, sizeof(saved));
	scene = saved;
	wrapped[3] = 7;
	CHECK(mh_scene_lws_decode(wrapped, size, &scene, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&scene, &saved, sizeof(scene)) == 0);
	CHECK(mh_scene_lws_decode(wrapped, 3, &scene, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&scene, &saved, sizeof(scene)) == 0);
	CHECK(mh_scene_lws_decode((const unsigned char *)"not a scene", 11,
		&scene, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&scene, &saved, sizeof(scene)) == 0);
	free(wrapped);
}

static void
check_interpolation(void)
{
	struct mh_scene_object object;
	struct mh_scene_object sampled;
	struct mh_scene_key keys[3];
	double values[9];
	double saved[9];
	unsigned int i;

	memset(&object, 0, sizeof(object));
	memset(keys, 0, sizeof(keys));
	object.keys = keys;
	object.key_count = 2;
	object.end_behavior = 1;
	keys[1].time = 1;
	keys[1].value[0] = 10;
	keys[1].value[3] = 720;
	keys[1].value[6] = 1;
	keys[0].tension = keys[1].tension = 1;
	keys[0].linear = 1;
	CHECK(mh_scene_channels(&object, 0.25, values, NULL) == MH_ERROR_NONE);

	/* Destination is cubic despite first flag. */
	CHECK(values[0] == 1.5625);
	keys[1].linear = 1;
	CHECK(mh_scene_channels(&object, 0.25, values, NULL) == MH_ERROR_NONE);
	CHECK(values[0] == 2.5 && values[3] == 180 && values[6] == 0.25);
	CHECK(mh_scene_sample(&object, 0.25, &sampled, NULL) == MH_ERROR_NONE);

	/* No shortest-angle shortcut. */
	CHECK(fabs(sampled.rotation[0] + 1) < 1e-12);
	CHECK(sampled.keys == keys && sampled.position[0] == 2.5);
	CHECK(mh_scene_channels(&object, 1, values, NULL) == MH_ERROR_NONE &&
		values[0] == 10);
	CHECK(mh_scene_channels(&object, 1e9, values, NULL) == MH_ERROR_NONE &&
		values[0] == 10);
	object.end_behavior = 2;
	CHECK(mh_scene_channels(&object, 1, values, NULL) == MH_ERROR_NONE &&
		values[0] == 0);
	CHECK(mh_scene_channels(&object, 100.25, values, NULL) ==
			MH_ERROR_NONE &&
		values[0] == 2.5);
	CHECK(mh_scene_channels(&object, 0.999999, values, NULL) ==
			MH_ERROR_NONE &&
		values[0] > 9.999);
	memcpy(saved, values, sizeof(values));
	CHECK(mh_scene_channels(&object, -1, values, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, values, sizeof(values)) == 0);
	CHECK(mh_scene_channels(&object, 1e10, values, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, values, sizeof(values)) == 0);
	object.key_count = 1; /* Single-key repeat must not divide by zero. */
	CHECK(mh_scene_channels(&object, 123, values, NULL) == MH_ERROR_NONE &&
		values[0] == 0);

	object.key_count = 2;
	object.end_behavior = 1;
	keys[0].tension = keys[1].tension = 0;
	keys[1].linear = 0;
	keys[0].continuity = 0.2;
	keys[0].bias = -0.3;
	keys[1].continuity = 0.5;
	keys[1].bias = 0.25;
	CHECK(mh_scene_channels(&object, 0.5, values, NULL) == MH_ERROR_NONE);

	/* Actual last-key tangent quirk. */
	CHECK(fabs(values[0] - 5.39375) < 1e-12);
	keys[0].continuity = keys[0].bias = keys[1].continuity = keys[1].bias =
		0;
	object.key_count = 3;
	keys[2].time = 3;
	keys[2].value[0] = 40;
	CHECK(mh_scene_channels(&object, 0.5, values, NULL) == MH_ERROR_NONE);
	CHECK(fabs(values[0] - 55.0 / 12.0) < 1e-12);
	CHECK(mh_scene_channels(&object, 1, values, NULL) == MH_ERROR_NONE &&
		values[0] == 10);
	CHECK(mh_scene_channels(&object, 2, values, NULL) == MH_ERROR_NONE);
	CHECK(fabs(values[0] - 295.0 / 12.0) < 1e-12);
	keys[2].time = 2;

	for (i = 0; i < 9; i++) {
		keys[0].value[i] = keys[1].value[i] = 0;
		keys[2].value[i] = 1;
	}

	CHECK(mh_scene_channels(&object, 0.5, values, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 9; i++)
		CHECK(values[i] == -0.0625); /* Preserve curve overshoot. */
}

int
main(void)
{
	static const char *suffixes[] = {"\nParentObject 1",
		"\nPivotPoint 1 2 3", "\nAddNullObject thing",
		"\nLoadObject new.lwo", "\nObjectMotion external.mot",
		"\nObjectMotion (unnamed)\n9\n1\n0 0 0 0 0 0 1 1 1\n0 0 0 0 0\nEndBehavior 1",
		"\nFramesPerSecond 25", "\nObjEdgeColor 1 2",
		"\nObjEdgeColor -1 0 0", "\nObjEdgeColor 65536 0 0",
		"\nObjEdgeColor nan 0 0", "\nObjEdgeColor 1 2.5 0",
		"\nPolygonSize -1", "\nPolygonSize nan", "\nPolygonSize 1 2",
		"\nPolygonSize 1\nPolygonSize 2", "\nUnseenByRays nan",
		"\nShadowOptions 2", "\nObjEdgeColor 1 2 0 extra",
		"\nObjEdgeColor 1 2 0\nObjEdgeColor 3 4 0"};

	struct mh_scene scene;
	struct mh_scene trial;
	struct mh_scene sentinel;
	char text[2048];
	char *at;
	enum mh_error_code status;
	unsigned int i;
	unsigned long seed;
	size_t length;

	check_interpolation();
	check_decode();
	memset(&scene, 0, sizeof(scene));
	status = mh_scene_lws_parse(
		(const unsigned char *)valid, sizeof(valid) - 1, &scene, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return 1;

	CHECK(scene.object_count == 2 && scene.key_count == 3 &&
		scene.frames_per_second == 24);
	CHECK(scene.objects[0].keys[1].time == 1);
	CHECK(scene.objects[0].has_polygon_size &&
		scene.objects[0].polygon_size == .2);
	CHECK(scene.objects[0].unseen_by_rays &&
		scene.objects[0].has_shadow_options);
	CHECK(!scene.objects[1].unseen_by_rays &&
		scene.objects[1].has_shadow_options &&
		scene.objects[1].polygon_size == 0);
	CHECK(scene.objects[0].has_edge_color &&
		scene.objects[0].sound_class == 11 &&
		scene.objects[0].collision_sound_class == 8);
	CHECK(!scene.objects[1].has_edge_color &&
		scene.objects[1].sound_class == 0 &&
		scene.objects[1].collision_sound_class == 0);
	CHECK(scene.objects[0].keys[1].linear == 1);
	CHECK(scene.objects[0].keys[1].value[3] == 180);
	CHECK(strcmp(scene.objects[0].model, "sign.myo") == 0);
	CHECK(scene.objects[0].position[0] == 10 &&
		scene.objects[0].position[1] == -2);
	CHECK(scene.objects[0].color[0] == 0 &&
		scene.objects[0].color[1] == 0.5);
	CHECK(fabs(scene.objects[0].rotation[2] + 1) < 1e-12);
	CHECK(fabs(scene.objects[0].rotation[6] - 1) < 1e-12);
	CHECK(fabs(scene.objects[1].rotation[5] - 1) < 1e-12);
	CHECK(fabs(scene.objects[1].rotation[7] + 1) < 1e-12);

	for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
		strcpy(text, valid);
		strcat(text, suffixes[i]);
		check_rejected(text, &scene);
	}

	for (i = 0; i < 9; i++) {
		strcpy(text, valid);

		if (i == 0)
			text[0] = 'X';

		if (i == 1)
			text[6] = '2';

		if (i == 2) {
			at = strstr(text, "9\n2");
			at[0] = '8';
		}

		if (i == 3) {
			at = strstr(text, "9\n2");
			at[2] = '0';
		}

		if (i == 4) {
			at = strstr(text, "10 -2");
			memcpy(at, "nan", 3);
		}

		if (i == 5) {
			at = strstr(text, "24 1 0");
			memcpy(at, "00", 2);
		}

		if (i == 6) {
			at = strstr(text, "EndBehavior 2");
			at[12] = '3';
		}

		if (i == 7)
			text[20] = (char)255;

		if (i == 8) {
			at = strstr(text, "0 0.5 1");
			at[0] = '-';
		}

		check_rejected(text, &scene);
	}

	/*
	 * Truncations can be valid after a complete object, but must be safe.
	 */
	memset(&sentinel, 0x5a, sizeof(sentinel));

	for (length = 0; length < sizeof(valid) - 1; length++) {
		trial = sentinel;
		status = mh_scene_lws_parse(
			(const unsigned char *)valid, length, &trial, NULL);

		if (status == MH_ERROR_NONE)
			mh_scene_free(&trial);
		else
			CHECK(memcmp(&trial, &sentinel, sizeof(trial)) == 0);
	}

	seed = 331;

	for (i = 0; i < 4000; i++) {
		memcpy(text, valid, sizeof(valid));
		seed = (seed * 1664525UL + 1013904223UL) & MH_U32_MAX;
		text[seed % (sizeof(valid) - 1)] = (char)(seed >> 16);
		trial = sentinel;
		status = mh_scene_lws_parse((const unsigned char *)text,
			sizeof(valid) - 1, &trial, NULL);

		if (status == MH_ERROR_NONE)
			mh_scene_free(&trial);
		else
			CHECK(memcmp(&trial, &sentinel, sizeof(trial)) == 0);
	}

	mh_scene_free(&scene);
	CHECK(scene.objects == NULL && scene.object_count == 0);

	if (failures != 0)
		return 1;

	puts("Scene poses, TCB/linear motion, repeats and malformed input passed");

	return 0;
}

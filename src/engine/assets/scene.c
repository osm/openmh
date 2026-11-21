#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/animation/scene.h"
#include "engine/assets/path_tracks.h"
#include "engine/assets/scene.h"
#include "engine/assets/scene_text.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define KEY_LIMIT 100000
#define MINIMUM_KEY_BYTES 28.0
#define OBJECT_LIMIT 4096
#define INITIAL_OBJECT_CAPACITY 16
#define DEFAULT_FRAMES_PER_SECOND 30.0
#define FPS_MINIMUM 0.001
#define FPS_MAXIMUM 1000
#define RGB_CHANNEL_LIMIT 100
#define POLYGON_SIZE_LIMIT 1e10

static int
read_model_name(const char *path, char output[MH_SCENE_MODEL_NAME_SIZE])
{
	const char *base;
	const char *dot;
	const char *cursor;
	size_t length;

	base = path;

	for (cursor = path; *cursor != 0; cursor++) {
		if (*cursor == '/' || *cursor == '\\')
			base = cursor + 1;

		if (*cursor == ' ' || *cursor == '\t')
			return 0;
	}

	dot = strrchr(base, '.');

	if (dot == NULL || dot == base)
		return 0;

	length = (size_t)(dot - base);

	if (length >
		MH_SCENE_MODEL_NAME_SIZE - sizeof(MH_ASSET_MODEL_EXTENSION))
		return 0;

	memcpy(output, base, length);
	memcpy(output + length, MH_ASSET_MODEL_EXTENSION,
		sizeof(MH_ASSET_MODEL_EXTENSION));

	for (length = 0; output[length] != 0; length++) {
		if (output[length] >= 'A' && output[length] <= 'Z')
			output[length] += 'a' - 'A';
	}

	return 1;
}

static enum mh_error_code
read_motion(struct mh_scene_text *input, struct mh_scene_object *object,
	int standalone, struct mh_error *error_out)
{
	double values[9];
	double metadata[5];
	double previous_frame;
	size_t i;
	unsigned int axis;

	if (object->key_count != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "duplicate ObjectMotion");

	if (mh_scene_text_next(input) != 1 ||
		!mh_scene_text_numbers(input->line, values, 1) ||
		values[0] != 9) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"scene motion requires nine channels");
	}

	if (mh_scene_text_next(input) != 1 ||
		!mh_scene_text_numbers(input->line, values, 1) ||
		values[0] < 1 || values[0] > KEY_LIMIT ||
		floor(values[0]) != values[0] ||
		values[0] > (double)(input->size - input->cursor) /
				MINIMUM_KEY_BYTES) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene motion key count");
	}

	object->key_count = (size_t)values[0];
	object->keys = (struct mh_scene_key *)calloc(
		object->key_count, sizeof(*object->keys));

	if (object->keys == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading motion keys");

	previous_frame = -1.0;

	for (i = 0; i < object->key_count; i++) {
		if (mh_scene_text_next(input) != 1 ||
			!mh_scene_text_numbers(input->line, values, 9) ||
			mh_scene_text_next(input) != 1 ||
			!mh_scene_text_numbers(input->line, metadata, 5)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene motion key");
		}

		if (metadata[0] <= previous_frame ||
			floor(metadata[0]) != metadata[0] ||
			(i == 0 && metadata[0] != 0) ||
			(metadata[1] != 0 && metadata[1] != 1))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported scene key timing");

		previous_frame = metadata[0];

		for (axis = 2; axis < 5; axis++) {
			if (metadata[axis] < -1 || metadata[axis] > 1)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported scene TCB parameter");
		}

		for (axis = 6; axis < 9; axis++) {
			if (values[axis] < 0 ||
				values[axis] > RGB_CHANNEL_LIMIT)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scene RGB channel");
		}

		if (i == 0)
			mh_scene_apply_channels(object, values);

		/* Convert frames after reading FPS. */
		object->keys[i].time = metadata[0];
		object->keys[i].linear = (int)metadata[1];
		object->keys[i].tension = metadata[2];
		object->keys[i].continuity = metadata[3];
		object->keys[i].bias = metadata[4];
		memcpy(object->keys[i].value, values, sizeof(values));
	}

	if (standalone) {
		object->end_behavior = MH_SCENE_REPEAT;
		return mh_scene_text_next(input) == 0
			? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: mh_error_set(error_out, MH_ERROR_INVALID,
				"unexpected text after LWMO keys");
	}

	if (mh_scene_text_next(input) != 1 ||
		strncmp(input->line, "EndBehavior", 11) != 0 ||
		(input->line[11] != ' ' && input->line[11] != '\t') ||
		!mh_scene_text_numbers(input->line + 12, values, 1) ||
		(values[0] != MH_SCENE_HOLD && values[0] != MH_SCENE_REPEAT))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported scene EndBehavior");

	object->end_behavior = (int)values[0];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_object_property(const char *key, const char *value,
	struct mh_scene_object *object, struct mh_error *error_out)
{
	enum mh_error_code status;
	double number, edge[3];
	unsigned int i;

	if (strcmp(key, "PolygonSize") == 0) {
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or misplaced scene PolygonSize");

		if (object == NULL || object->has_polygon_size ||
			!mh_scene_text_numbers(value, &number, 1) ||
			number < 0 || number > POLYGON_SIZE_LIMIT)
			return status;

		object->polygon_size = number;
		object->has_polygon_size = 1;
	} else if (strcmp(key, "UnseenByRays") == 0) {
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or misplaced scene UnseenByRays");

		if (object == NULL || object->unseen_by_rays ||
			!mh_scene_text_numbers(value, &number, 1))
			return status;

		/*
		 * Original tests presence, not the supplied numeric
		 * value.
		 */
		object->unseen_by_rays = 1;
	} else if (strcmp(key, "ShadowOptions") == 0) {
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or misplaced scene ShadowOptions");

		if (object == NULL || object->has_shadow_options ||
			!mh_scene_text_numbers(value, &number, 1))
			return status;

		object->has_shadow_options = 1;
	} else if (strcmp(key, "ObjEdgeColor") == 0) {
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or misplaced scene ObjEdgeColor");

		if (object == NULL || object->has_edge_color ||
			!mh_scene_text_numbers(value, edge, 3))
			return status;

		for (i = 0; i < 3; i++)
			if (edge[i] < 0 || edge[i] > MH_U16_MAX ||
				floor(edge[i]) != edge[i])
				return status;

		object->sound_class = (unsigned int)edge[0];
		object->collision_sound_class = (unsigned int)edge[1];
		object->has_edge_color = 1;
	} else if (strcmp(key, "ParentObject") == 0 ||
		strcmp(key, "PivotPoint") == 0 ||
		strcmp(key, "AddNullObject") == 0) {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"scene parenting, pivots and null objects are not supported");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_scene_lws_parse(const unsigned char *text, size_t size,
	struct mh_scene *scene, struct mh_error *error_out)
{
	struct mh_scene_text input;
	struct mh_scene parsed;
	struct mh_scene_object *object;
	struct mh_scene_object *grown;
	enum mh_error_code status;
	char *value;
	size_t capacity;
	size_t i;
	size_t j;
	double number;
	int line_status;
	int have_fps;

	if (text == NULL || scene == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scene input/output");

	memset(&parsed, 0, sizeof(parsed));
	memset(&input, 0, sizeof(input));
	input.text = text;
	input.size = size;
	parsed.frames_per_second = DEFAULT_FRAMES_PER_SECOND;
	capacity = 0;
	have_fps = 0;
	object = NULL;
	status = mh_error_set(
		error_out, MH_ERROR_INVALID, "expected LWSC version 1");

	if (mh_scene_text_next(&input) != 1 ||
		strcmp(input.line, "LWSC") != 0 ||
		mh_scene_text_next(&input) != 1 || strcmp(input.line, "1") != 0)
		goto fail;

	while ((line_status = mh_scene_text_next(&input)) == 1) {
		value = input.line + strcspn(input.line, " \t");

		if (*value != 0)
			*value++ = 0;

		while (*value == ' ' || *value == '\t')
			value++;

		if (strcmp(input.line, "FramesPerSecond") == 0) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene FramesPerSecond");

			if (have_fps ||
				!mh_scene_text_numbers(value, &number, 1) ||
				number < FPS_MINIMUM || number > FPS_MAXIMUM)
				goto fail;

			parsed.frames_per_second = number;
			have_fps = 1;
		} else if (strcmp(input.line, "LoadObject") == 0) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"scene object is missing motion");

			if (object != NULL && object->key_count == 0)
				goto fail;

			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"too many scene objects");

			if (parsed.object_count == OBJECT_LIMIT)
				goto fail;

			if (parsed.object_count == capacity) {
				capacity = capacity == 0
					? INITIAL_OBJECT_CAPACITY
					: capacity * 2;
				grown = (struct mh_scene_object *)realloc(
					parsed.objects,
					capacity * sizeof(*grown));
				status =
					mh_error_set(error_out, MH_ERROR_MEMORY,
						"out of memory reading scene");

				if (grown == NULL)
					goto fail;

				parsed.objects = grown;
			}

			object = &parsed.objects[parsed.object_count++];
			memset(object, 0, sizeof(*object));
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene model name");

			if (!read_model_name(value, object->model))
				goto fail;
		} else if (strcmp(input.line, "ObjectMotion") == 0) {
			status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported or misplaced ObjectMotion");

			if (object == NULL || strcmp(value, "(unnamed)") != 0)
				goto fail;

			status = read_motion(&input, object, 0, error_out);

			if (status != MH_ERROR_NONE)
				goto fail;

			parsed.key_count += object->key_count;
		} else {
			status = read_object_property(
				input.line, value, object, error_out);

			if (status != MH_ERROR_NONE)
				goto fail;
		}
	}

	status = mh_error_set(error_out, MH_ERROR_INVALID,
		"invalid scene text or missing object motion");

	if (line_status < 0 || object == NULL || object->key_count == 0)
		goto fail;

	for (i = 0; i < parsed.object_count; i++) {
		for (j = 0; j < parsed.objects[i].key_count; j++) {
			parsed.objects[i].keys[j].time /=
				parsed.frames_per_second;
		}
	}

	*scene = parsed;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

fail:
	mh_scene_free(&parsed);

	return status;
}

/*
 * LWMO uses the same keys as LWSC, without EndBehavior or FPS. Track loading
 * supplies time scale 1 and enables repeat.
 */
enum mh_error_code
mh_motion_mot_parse(const unsigned char *text, size_t size,
	struct mh_scene_object *output, struct mh_error *error_out)
{
	struct mh_scene_text input;
	struct mh_scene_object parsed;
	enum mh_error_code status;

	if (text == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing motion input/output");

	memset(&input, 0, sizeof(input));
	memset(&parsed, 0, sizeof(parsed));
	input.text = text;
	input.size = size;

	if (mh_scene_text_next(&input) != 1 ||
		strcmp(input.line, "LWMO") != 0 ||
		mh_scene_text_next(&input) != 1 || strcmp(input.line, "1") != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "expected LWMO version 1");

	status = read_motion(&input, &parsed, 1, error_out);

	if (status != MH_ERROR_NONE) {
		free(parsed.keys);
		return status;
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

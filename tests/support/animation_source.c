#include <stdlib.h>
#include <string.h>

#include "animation_source.h"
#include "engine/animation/scene.h"
#include "engine/assets/scene.h"
#include "engine/storage/asset_path.h"

#define FIXTURE_EXTENSION ".fixtureanim"
#define FIXTURE_PATH_BYTES 1024

void
mh_test_animation_source_free(struct mh_test_animation_source *source)
{
	size_t i;

	for (i = 0; i < source->count; i++)
		mh_scene_free(&source->scenes[i]);

	memset(source, 0, sizeof(*source));
}

static enum mh_error_code
find_animation(struct mh_test_animation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, int motion, const struct mh_scene **output,
	struct mh_error *error_out)
{
	struct mh_file file;
	char relative[FIXTURE_PATH_BYTES], path[FIXTURE_PATH_BYTES];
	size_t length, index;
	enum mh_error_code status;

	length = strlen(reference);

	if (length < 4 || reference[length - 4] != '.' ||
		length - 4 + sizeof(FIXTURE_EXTENSION) > sizeof(relative))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid animation fixture reference");

	memcpy(relative, reference, length - 4);
	strcpy(relative + length - 4, FIXTURE_EXTENSION);
	status = mh_asset_path_from(
		resources, root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	index = file.size == 2 ? file.data[0] + (size_t)file.data[1] * 256
			: source->count;
	free(file.data);

	if (index >= source->count || source->motion[index] != motion)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid animation fixture key");

	*output = &source->scenes[index];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
copy_object(const struct mh_scene_object *input, struct mh_scene_object *output,
	struct mh_error *error_out)
{
	struct mh_scene_object object;

	object = *input;
	object.keys = malloc(object.key_count * sizeof(*object.keys));

	if (object.keys == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying animation keys");

	memcpy(object.keys, input->keys,
		object.key_count * sizeof(*object.keys));
	*output = object;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_scene(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_scene *output,
	struct mh_error *error_out)
{
	struct mh_test_animation_source *source;
	const struct mh_scene *input;
	struct mh_scene scene;
	size_t i;
	enum mh_error_code status;

	source = context;
	status = find_animation(
		source, resources, root, reference, 0, &input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	scene = *input;
	scene.objects = calloc(scene.object_count, sizeof(*scene.objects));

	if (scene.objects == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying animation scene");

	for (i = 0; i < scene.object_count; i++) {
		status = copy_object(
			&input->objects[i], &scene.objects[i], error_out);

		if (status != MH_ERROR_NONE) {
			mh_scene_free(&scene);
			return status;
		}
	}

	source->scene_reads++;
	*output = scene;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_motion(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_scene_object *output,
	struct mh_error *error_out)
{
	struct mh_test_animation_source *source;
	const struct mh_scene *input;
	enum mh_error_code status;

	source = context;
	status = find_animation(
		source, resources, root, reference, 1, &input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = copy_object(&input->objects[0], output, error_out);

	if (status == MH_ERROR_NONE)
		source->motion_reads++;

	return status;
}

void
mh_test_animation_source_init(struct mh_test_animation_source *source,
	struct mh_animation_source *provider)
{
	provider->context = source;

	provider->load_scene = load_scene;
	provider->load_motion = load_motion;
}

enum mh_error_code
mh_test_animation_convert(struct mh_test_animation_source *source,
	struct mh_resource_entry *entry, int motion, struct mh_error *error_out)
{
	struct mh_scene scene;
	char *path;
	unsigned char *data;
	size_t length;
	enum mh_error_code status;

	length = strlen(entry->path);

	if (length < 4 || source->count == MH_TEST_ANIMATIONS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid animation fixture conversion");

	path = malloc(length - 4 + sizeof(FIXTURE_EXTENSION));
	data = malloc(2);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting animation fixture");
	}

	memset(&scene, 0, sizeof(scene));

	if (motion) {
		scene.objects = calloc(1, sizeof(*scene.objects));
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting motion fixture");

		if (scene.objects != NULL) {
			scene.object_count = 1;
			status = mh_motion_mot_decode(entry->data, entry->size,
				scene.objects, error_out);
			scene.key_count = scene.objects[0].key_count;
		}
	} else {
		status = mh_scene_lws_decode(
			entry->data, entry->size, &scene, error_out);
	}

	if (status != MH_ERROR_NONE) {
		mh_scene_free(&scene);
		free(path);
		free(data);
		return status;
	}

	memcpy(path, entry->path, length - 4);
	strcpy(path + length - 4, FIXTURE_EXTENSION);
	data[0] = (unsigned char)(source->count & 255);
	data[1] = (unsigned char)(source->count >> 8);
	source->scenes[source->count] = scene;
	source->motion[source->count] = motion;
	source->count++;
	free((void *)entry->path);
	free((void *)entry->data);
	entry->path = path;
	entry->data = data;
	entry->size = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/animation/scene.h"
#include "engine/assets/scene.h"
#include "engine/core/error.h"

enum {
	SEGMENT_SAMPLES = 4
};

#define MAX_SECONDS 1e9
#define END_SAMPLE_OFFSET 0.125

static enum mh_error_code
print_sample(const struct mh_scene_object *object, size_t index, double seconds,
	struct mh_error *error_out)
{
	double values[9];
	unsigned int i;
	enum mh_error_code status;

	status = mh_scene_channels(object, seconds, values, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	printf("%lu\t%.17g", (unsigned long)index, seconds);

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
		printf("\t%.17g", values[i]);

	putchar('\n');

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
print_motion_samples(const struct mh_scene_object *object, size_t index,
	struct mh_error *error_out)
{
	size_t key;
	unsigned int j;
	double time;
	enum mh_error_code status;

	/*
	 * Every key and three interior points per segment, plus
	 * a point past the end to distinguish hold from repeat.
	 */
	for (key = 0; key < object->key_count; key++) {
		for (j = 0; j <
			(key + 1 < object->key_count ? SEGMENT_SAMPLES : 1U);
			j++) {
			time = object->keys[key].time;

			if (j != 0)
				time += (object->keys[key + 1].time - time) *
					j / (double)SEGMENT_SAMPLES;

			status = print_sample(object, index, time, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	time = object->keys[object->key_count - 1].time + END_SAMPLE_OFFSET;

	if (time > MAX_SECONDS)
		time = MAX_SECONDS;

	return print_sample(object, index, time, error_out);
}

static enum mh_error_code
print_pose(const struct mh_scene_object *object, double seconds,
	struct mh_error *error_out)
{
	struct mh_scene_object sampled;
	unsigned int j;
	enum mh_error_code status;

	status = mh_scene_sample(object, seconds, &sampled, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	object = &sampled;
	printf("%s\t%lu\t%d", object->model, (unsigned long)object->key_count,
		object->end_behavior);

	for (j = 0; j < 3; j++)
		printf("\t%.12g", object->position[j]);

	for (j = 0; j < 9; j++)
		printf("\t%.12g", object->rotation[j]);

	for (j = 0; j < 3; j++)
		printf("\t%.12g", object->color[j]);

	putchar('\n');

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_scene(const char *path, struct mh_scene *scene, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_file_read(path, &file, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_scene_lws_decode(
			file.data, file.size, scene, error_out);
		free(file.data);
	}

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_scene scene;
	const struct mh_scene_object *object;
	size_t i;
	struct mh_error error;
	enum mh_error_code status;
	char *end;
	double seconds;
	int samples, classes;

	if (argc != 2 && argc != 3) {
		fprintf(stderr,
			"Usage: mh-scene-info FILE.lws [SECONDS | --samples | --classes]\n");
		return 1;
	}

	seconds = 0.0;
	samples = argc == 3 && strcmp(argv[2], "--samples") == 0;
	classes = argc == 3 && strcmp(argv[2], "--classes") == 0;

	if (argc == 3 && !samples && !classes) {
		errno = 0;
		seconds = strtod(argv[2], &end);

		if (errno != 0 || end == argv[2] || *end != 0 ||
			!(seconds >= 0 && seconds <= MAX_SECONDS)) {
			fprintf(stderr,
				"Time must be a finite number between 0 and 1e9 seconds.\n");
			return 1;
		}
	}

	status = read_scene(argv[1], &scene, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", argv[1], error.message);
		return 1;
	}

	printf("scene\t%lu\t%lu\t%.12g\n", (unsigned long)scene.object_count,
		(unsigned long)scene.key_count, scene.frames_per_second);

	for (i = 0; i < scene.object_count; i++) {
		object = &scene.objects[i];

		if (classes) {
			printf("%lu\t%s\t%u\t%u\n", (unsigned long)i,
				object->model, object->sound_class,
				object->collision_sound_class);
			continue;
		}

		if (samples) {
			status = print_motion_samples(object, i, &error);

			if (status != MH_ERROR_NONE)
				goto fail;

			continue;
		}

		status = print_pose(object, seconds, &error);

		if (status != MH_ERROR_NONE)
			goto fail;
	}

	mh_scene_free(&scene);
	return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;

fail:
	fprintf(stderr, "Scene sample: %s\n", error.message);
	mh_scene_free(&scene);

	return 1;
}

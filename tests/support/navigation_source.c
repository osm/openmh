#include <stdlib.h>
#include <string.h>

#include "engine/assets/ai_path.h"
#include "engine/assets/camera_motion.h"
#include "engine/assets/route.h"
#include "engine/storage/asset_path.h"
#include "navigation_source.h"

#define FIXTURE_EXTENSION ".fixturenav"
#define FIXTURE_PATH_BYTES 1024

void
mh_test_navigation_source_free(struct mh_test_navigation_source *source)
{
	mh_route_free(&source->route);
	mh_camera_motion_free(&source->camera);
	mh_ai_path_free(&source->ai);
	memset(source, 0, sizeof(*source));
}

static enum mh_error_code
read_kind(struct mh_test_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, enum mh_test_navigation_kind kind,
	size_t byte_limit, struct mh_error *error_out)
{
	struct mh_file file;
	char relative[FIXTURE_PATH_BYTES], path[FIXTURE_PATH_BYTES];
	size_t length;
	enum mh_error_code status;
	int valid;

	length = strlen(reference);

	if (length < 4 || reference[length - 4] != '.' ||
		length - 4 + sizeof(FIXTURE_EXTENSION) > sizeof(relative))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid navigation fixture reference");

	memcpy(relative, reference, length - 4);
	strcpy(relative + length - 4, FIXTURE_EXTENSION);
	status = mh_asset_path_from(
		resources, root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		resources, path, byte_limit, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	valid = file.size == 1 && file.data[0] == kind && source->present[kind];
	free(file.data);

	if (!valid)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid navigation fixture kind");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_route(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, size_t byte_limit,
	struct mh_route *output, struct mh_error *error_out)
{
	struct mh_test_navigation_source *source;
	struct mh_route route;
	enum mh_error_code status;

	source = context;
	source->route_limit = byte_limit;
	status = read_kind(source, resources, root, reference, MH_TEST_ROUTE,
		byte_limit, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	route = source->route;
	route.motion.keys =
		malloc(route.motion.key_count * sizeof(*route.motion.keys));

	if (route.motion.keys == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying route fixture");

	memcpy(route.motion.keys, source->route.motion.keys,
		route.motion.key_count * sizeof(*route.motion.keys));
	source->reads[MH_TEST_ROUTE]++;
	*output = route;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_camera(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference,
	struct mh_camera_motion *output, struct mh_error *error_out)
{
	struct mh_test_navigation_source *source;
	struct mh_camera_motion camera;
	enum mh_error_code status;

	source = context;
	status = read_kind(source, resources, root, reference, MH_TEST_CAMERA,
		MH_RESOURCE_READ_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	camera = source->camera;
	camera.keys = malloc(camera.count * sizeof(*camera.keys));

	if (camera.keys == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying camera fixture");

	memcpy(camera.keys, source->camera.keys,
		camera.count * sizeof(*camera.keys));
	source->reads[MH_TEST_CAMERA]++;
	*output = camera;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_ai(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_ai_path *output,
	struct mh_error *error_out)
{
	struct mh_test_navigation_source *source;
	struct mh_ai_path path;
	enum mh_error_code status;

	source = context;
	status = read_kind(source, resources, root, reference, MH_TEST_AI,
		MH_RESOURCE_READ_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path = source->ai;
	path.points = malloc(path.count * sizeof(*path.points));
	path.sections = malloc(path.section_count * sizeof(*path.sections));

	if (path.points == NULL || path.sections == NULL) {
		mh_ai_path_free(&path);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying AI fixture");
	}

	memcpy(path.points, source->ai.points,
		path.count * sizeof(*path.points));
	memcpy(path.sections, source->ai.sections,
		path.section_count * sizeof(*path.sections));
	source->reads[MH_TEST_AI]++;
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_navigation_source_init(struct mh_test_navigation_source *source,
	struct mh_navigation_source *provider)
{
	provider->context = source;

	provider->load_route = load_route;
	provider->load_camera = load_camera;
	provider->load_ai_path = load_ai;
}

enum mh_error_code
mh_test_navigation_convert(struct mh_test_navigation_source *source,
	struct mh_resource_entry *entry, enum mh_test_navigation_kind kind,
	struct mh_error *error_out)
{
	char *path;
	unsigned char *data;
	size_t length;
	enum mh_error_code status;

	length = strlen(entry->path);

	if (length < 4 || kind < MH_TEST_ROUTE ||
		kind >= MH_TEST_NAVIGATION_KINDS || source->present[kind])
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid navigation fixture conversion");

	path = malloc(length - 4 + sizeof(FIXTURE_EXTENSION));
	data = malloc(1);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting navigation fixture");
	}

	if (kind == MH_TEST_ROUTE)
		status = mh_route_mot_decode(
			entry->data, entry->size, &source->route, error_out);
	else if (kind == MH_TEST_CAMERA)
		status = mh_camera_mot_parse(
			entry->data, entry->size, &source->camera, error_out);
	else
		status = mh_ai_path_dat_decode(
			entry->data, entry->size, &source->ai, error_out);

	if (status != MH_ERROR_NONE) {
		free(path);
		free(data);
		return status;
	}

	source->present[kind] = 1;
	memcpy(path, entry->path, length - 4);
	strcpy(path + length - 4, FIXTURE_EXTENSION);
	data[0] = (unsigned char)kind;
	free((void *)entry->path);
	free((void *)entry->data);
	entry->path = path;
	entry->data = data;
	entry->size = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

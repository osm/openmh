#include <stdlib.h>
#include <string.h>

#include "engine/assets/lob.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "wireframe_source.h"

#define FIXTURE_EXTENSION ".fixturewireframe"
#define FIXTURE_PATH_BYTES 1024

void
mh_test_wireframe_source_free(struct mh_test_wireframe_source *source)
{
	size_t i;

	for (i = 0; i < source->count; i++)
		mh_wireframe_free(&source->meshes[i]);

	memset(source, 0, sizeof(*source));
}

static enum mh_error_code
load_wireframe(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_wireframe *output,
	struct mh_error *error_out)
{
	struct mh_test_wireframe_source *source;
	struct mh_wireframe mesh;
	struct mh_file file;
	char relative[FIXTURE_PATH_BYTES], path[FIXTURE_PATH_BYTES];
	size_t length, index;
	enum mh_error_code status;

	source = context;
	length = strlen(reference);

	if (length < 4 || !mh_ascii_equal(reference + length - 4, ".lob") ||
		length - 4 + sizeof(FIXTURE_EXTENSION) > sizeof(relative))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid wireframe fixture reference");

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

	if (index >= source->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid wireframe fixture key");

	mesh = source->meshes[index];
	mesh.points = malloc(mesh.point_count * sizeof(*mesh.points));
	mesh.edges = mesh.edge_count == 0
		? NULL
		: malloc(mesh.edge_count * sizeof(*mesh.edges));

	if (mesh.points == NULL ||
		(mesh.edge_count != 0 && mesh.edges == NULL)) {
		mh_wireframe_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying wireframe fixture");
	}

	memcpy(mesh.points, source->meshes[index].points,
		mesh.point_count * sizeof(*mesh.points));

	if (mesh.edge_count != 0)
		memcpy(mesh.edges, source->meshes[index].edges,
			mesh.edge_count * sizeof(*mesh.edges));

	source->reads++;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_wireframe_source_init(struct mh_test_wireframe_source *source,
	struct mh_wireframe_source *provider)
{
	provider->context = source;

	provider->load = load_wireframe;
}

enum mh_error_code
mh_test_wireframe_convert(struct mh_test_wireframe_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out)
{
	char *path;
	unsigned char *data;
	size_t length;
	enum mh_error_code status;

	length = strlen(entry->path);

	if (length < 4 || !mh_ascii_equal(entry->path + length - 4, ".lob"))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (source->count == MH_TEST_WIREFRAMES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many wireframe fixtures");

	path = malloc(length - 4 + sizeof(FIXTURE_EXTENSION));
	data = malloc(2);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting wireframe fixture");
	}

	status = mh_wireframe_lob_decode(entry->data, entry->size,
		&source->meshes[source->count], error_out);

	if (status != MH_ERROR_NONE) {
		free(path);
		free(data);
		return status;
	}

	memcpy(path, entry->path, length - 4);
	strcpy(path + length - 4, FIXTURE_EXTENSION);
	data[0] = (unsigned char)(source->count & 255);
	data[1] = (unsigned char)(source->count >> 8);
	source->count++;
	free((void *)entry->path);
	free((void *)entry->data);
	entry->path = path;
	entry->data = data;
	entry->size = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

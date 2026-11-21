#include <stdlib.h>
#include <string.h>

#include "atlas_source.h"
#include "engine/assets/remap.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"

#define FIXTURE_EXTENSION ".fixtureatlas"
#define FIXTURE_PATH_BYTES 1024

void
mh_test_atlas_source_free(struct mh_test_atlas_source *source)
{
	size_t i;

	for (i = 0; i < source->count; i++)
		mh_atlas_map_free(&source->maps[i]);

	memset(source, 0, sizeof(*source));
}

static enum mh_error_code
load_map(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_atlas_map *output,
	struct mh_error *error_out)
{
	struct mh_test_atlas_source *source;
	struct mh_atlas_map map;
	struct mh_file file;
	char relative[FIXTURE_PATH_BYTES], path[FIXTURE_PATH_BYTES];
	size_t length, index;
	enum mh_error_code status;

	source = context;
	length = strlen(reference);

	if (length < 4 || !mh_ascii_equal(reference + length - 4, ".bin") ||
		length - 4 + sizeof(FIXTURE_EXTENSION) > sizeof(relative))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid atlas fixture reference");

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
			"invalid atlas fixture key");

	map = source->maps[index];
	map.entries = malloc(map.count * sizeof(*map.entries));

	if (map.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying atlas fixture");

	memcpy(map.entries, source->maps[index].entries,
		map.count * sizeof(*map.entries));
	source->reads++;
	*output = map;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_atlas_source_init(
	struct mh_test_atlas_source *source, struct mh_atlas_source *provider)
{
	provider->context = source;

	provider->load = load_map;
}

enum mh_error_code
mh_test_atlas_convert(struct mh_test_atlas_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out)
{
	const char *basename;
	char *path;
	unsigned char *data;
	size_t length;
	enum mh_error_code status;

	length = strlen(entry->path);
	basename = strrchr(entry->path, '/');
	basename = basename == NULL ? entry->path : basename + 1;

	if (length < 4 || !mh_ascii_equal(entry->path + length - 4, ".bin") ||
		!mh_ascii_equal_n(basename, "remap", 5))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (source->count == MH_TEST_ATLAS_MAPS)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many atlas fixtures");

	path = malloc(length - 4 + sizeof(FIXTURE_EXTENSION));
	data = malloc(2);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting atlas fixture");
	}

	status = mh_atlas_remap_decode(entry->data, entry->size,
		&source->maps[source->count], error_out);

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

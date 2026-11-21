#include <stdlib.h>
#include <string.h>

#include "backends/collision/col_provider.h"
#include "collision_source.h"
#include "engine/collision/data.h"
#include "engine/core/text.h"
#include "interfaces/collision/decoder.h"

static void *
copy_array(const void *data, size_t count, size_t element_size)
{
	void *copy;

	if (count == 0 || count > (size_t)-1 / element_size)
		return NULL;

	copy = malloc(count * element_size);

	if (copy != NULL)
		memcpy(copy, data, count * element_size);

	return copy;
}

static enum mh_error_code
copy_collision(const struct mh_collision *source, struct mh_collision *output,
	struct mh_error *error_out)
{
	struct mh_collision copy;

	copy = *source;
	copy.cells = copy_array(
		source->cells, source->root_count, sizeof(*source->cells));
	copy.nodes = copy_array(
		source->nodes, source->node_count, sizeof(*source->nodes));
	copy.leaves = copy_array(
		source->leaves, source->leaf_count, sizeof(*source->leaves));
	copy.polygon_indices = copy_array(source->polygon_indices,
		source->polygon_index_count, sizeof(*source->polygon_indices));
	copy.polygons = copy_array(source->polygons,
		(size_t)source->polygon_count, sizeof(*source->polygons));

	if ((copy.root_count != 0 && copy.cells == NULL) ||
		(copy.node_count != 0 && copy.nodes == NULL) ||
		(copy.leaf_count != 0 && copy.leaves == NULL) ||
		(copy.polygon_index_count != 0 &&
			copy.polygon_indices == NULL) ||
		(copy.polygon_count != 0 && copy.polygons == NULL)) {
		mh_collision_free(&copy);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying collision fixture");
	}

	*output = copy;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
fixture_decode(void *context, const unsigned char *data, size_t size,
	struct mh_collision *output, struct mh_error *error_out)
{
	struct mh_test_collision_source *source;

	source = context;

	if (size != 1 || data[0] >= source->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid alternate collision fixture");

	source->reads++;

	return copy_collision(&source->collisions[data[0]], output, error_out);
}

void
mh_test_collision_source_init(struct mh_test_collision_source *source,
	struct mh_collision_source *decoder)
{
	decoder->context = source;

	decoder->extension = "fixture";
	decoder->read = NULL;
	decoder->decode = fixture_decode;
}

void
mh_test_collision_source_free(struct mh_test_collision_source *source)
{
	size_t i;

	for (i = 0; i < source->count; i++)
		mh_collision_free(&source->collisions[i]);

	memset(source, 0, sizeof(*source));
}

enum mh_error_code
mh_test_collision_convert(struct mh_test_collision_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out)
{
	struct mh_collision collision;
	char *path;
	unsigned char *data;
	size_t length;
	enum mh_error_code status;

	length = strlen(entry->path);

	if (length < 4 || !mh_ascii_equal(entry->path + length - 4, ".col"))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (source->count == MH_TEST_COLLISIONS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many collision fixtures");

	status = mh_collision_decode(mh_collision_col_decoder(), entry->data,
		entry->size, &collision, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path = malloc(length - 4 + sizeof(".fixture"));
	data = malloc(1);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		mh_collision_free(&collision);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting collision fixture");
	}

	memcpy(path, entry->path, length - 4);
	strcpy(path + length - 4, ".fixture");
	data[0] = (unsigned char)source->count;
	source->collisions[source->count++] = collision;
	free((void *)entry->path);
	free((void *)entry->data);
	entry->path = path;
	entry->data = data;
	entry->size = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

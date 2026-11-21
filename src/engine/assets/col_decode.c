#include <stdlib.h>
#include <string.h>

#include "engine/assets/col.h"
#include "engine/assets/col_internal.h"
#include "engine/collision/data.h"
#include "engine/core/binary.h"

static void *
allocate_array(size_t count, size_t element_size)
{
	if (count == 0 || count > (size_t)-1 / element_size)
		return NULL;

	return calloc(count, element_size);
}

static void
count_lists(const struct mh_col *col, struct mh_collision *collision)
{
	size_t unit, count;

	for (unit = 1; unit < col->list_units; unit += (count + 2) / 2) {
		count = (size_t)mh_read_u16le(
			col->data + col->list_offset + unit * LIST_UNIT_BYTES);
		collision->leaf_count++;
		collision->polygon_index_count += count;
	}
}

static enum mh_error_code
allocate_collision(const struct mh_col *col, struct mh_collision *collision,
	struct mh_error *error_out)
{
	collision->depth = col->depth;

	memcpy(collision->dimensions, col->dimensions, sizeof(col->dimensions));
	memcpy(collision->roots, col->roots, sizeof(col->roots));
	memcpy(collision->translation, col->translation,
		sizeof(col->translation));
	collision->scale = col->scale;
	collision->cell_size = col->cell_size;
	collision->root_count = col->root_count;

	/* COL node zero is an unused sentinel, not an octree branch. */
	collision->node_count = (size_t)col->node_count - 1;
	collision->polygon_count = col->polygon_count;
	count_lists(col, collision);

	collision->cells = (struct mh_collision_cell *)allocate_array(
		collision->root_count, sizeof(*collision->cells));
	collision->nodes = (struct mh_collision_node *)allocate_array(
		collision->node_count, sizeof(*collision->nodes));
	collision->leaves = (struct mh_collision_leaf *)allocate_array(
		collision->leaf_count, sizeof(*collision->leaves));
	collision->polygon_indices =
		(unsigned long *)allocate_array(collision->polygon_index_count,
			sizeof(*collision->polygon_indices));
	collision->polygons = (struct mh_collision_polygon *)allocate_array(
		(size_t)collision->polygon_count, sizeof(*collision->polygons));

	if (collision->cells == NULL ||
		(collision->node_count != 0 && collision->nodes == NULL) ||
		(collision->leaf_count != 0 && collision->leaves == NULL) ||
		(collision->polygon_index_count != 0 &&
			collision->polygon_indices == NULL) ||
		(collision->polygon_count != 0 && collision->polygons == NULL))
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding COL collision data");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
decode_lists(const struct mh_col *col, struct mh_collision *collision,
	size_t *list_map)
{
	struct mh_collision_leaf *leaf;
	const unsigned char *list;
	size_t unit, count, index, first, number;

	first = 0;
	number = 0;

	for (unit = 1; unit < col->list_units; unit += (count + 2) / 2) {
		list = col->data + col->list_offset + unit * LIST_UNIT_BYTES;
		count = (size_t)mh_read_u16le(list);
		list_map[unit] = number;
		leaf = &collision->leaves[number++];
		leaf->first = first;
		leaf->count = count;

		for (index = 0; index < count; index++)
			collision->polygon_indices[first++] = mh_read_u16le(
				list + INDEX_BYTES + index * INDEX_BYTES);
	}
}

static void
decode_reference(unsigned long reference, const size_t *list_map,
	struct mh_collision_cell *cell)
{
	if (reference == 0) {
		cell->kind = MH_COLLISION_EMPTY;
		cell->index = 0;
	} else if ((reference & LIST_REFERENCE_BIT) != 0) {
		cell->kind = MH_COLLISION_LEAF;
		cell->index = list_map[reference & REFERENCE_INDEX_MASK];
	} else {
		cell->kind = MH_COLLISION_NODE;
		cell->index = (size_t)reference - 1;
	}
}

static void
decode_cells(const struct mh_col *col, struct mh_collision *collision,
	const size_t *list_map)
{
	size_t index;
	unsigned int child;
	const unsigned char *node;

	for (index = 0; index < collision->root_count; index++)
		decode_reference(mh_read_u16le(col->data + HEADER_BYTES +
					index * INDEX_BYTES),
			list_map, &collision->cells[index]);

	for (index = 0; index < collision->node_count; index++) {
		node = col->data + col->node_offset + (index + 1) * NODE_BYTES;

		for (child = 0; child < NODE_CHILDREN; child++)
			decode_reference(
				mh_read_u16le(node + child * INDEX_BYTES),
				list_map,
				&collision->nodes[index].children[child]);
	}
}

static void
decode_polygons(const struct mh_col *col, struct mh_collision *collision)
{
	struct mh_col_polygon source;
	struct mh_collision_polygon *polygon;
	unsigned long index;

	for (index = 0; index < col->polygon_count; index++) {
		/* The raw reader has already validated every polygon. */
		mh_col_polygon(col, index, &source);
		polygon = &collision->polygons[index];
		polygon->material = source.material;
		polygon->boundary_count = source.boundary_count;
		memcpy(polygon->plane, source.plane, sizeof(polygon->plane));
		memcpy(polygon->boundaries, source.boundaries,
			sizeof(polygon->boundaries));
	}
}

enum mh_error_code
mh_collision_col_decode(const unsigned char *data, size_t size,
	struct mh_collision *output, struct mh_error *error_out)
{
	struct mh_col col;
	struct mh_collision collision;
	size_t *list_map;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing decoded collision output");

	status = mh_col_parse(data, size, &col, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&collision, 0, sizeof(collision));
	list_map = NULL;
	status = allocate_collision(&col, &collision, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	list_map = (size_t *)allocate_array(
		(size_t)col.list_units, sizeof(*list_map));

	if (list_map == NULL) {
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding COL list references");
		goto done;
	}

	decode_lists(&col, &collision, list_map);
	decode_cells(&col, &collision, list_map);
	decode_polygons(&col, &collision);
	*output = collision;
	memset(&collision, 0, sizeof(collision));

done:
	free(list_map);
	mh_collision_free(&collision);

	return status;
}

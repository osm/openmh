#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/col.h"
#include "engine/assets/col_internal.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define NORMAL_SQUARED_TOLERANCE 0.0001
#define RECIPROCAL_TOLERANCE 0.000001

static int
read_plane(const unsigned char *record, double result[4])
{
	unsigned int i;
	double normal_squared;

	for (i = 0; i < 4; i++) {
		if (!mh_read_f32le(record + i * 4, &result[i]))
			return 0;
	}

	normal_squared = result[0] * result[0] + result[1] * result[1] +
		result[2] * result[2];

	return fabs(normal_squared - 1.0) <= NORMAL_SQUARED_TOLERANCE;
}

int
mh_col_polygon(const struct mh_col *col, unsigned long index,
	struct mh_col_polygon *polygon)
{
	struct mh_col_polygon parsed;
	const unsigned char *record;
	unsigned long reference, boundary;
	unsigned int i;
	unsigned int j;

	if (index >= col->polygon_count)
		return 0;

	memset(&parsed, 0, sizeof(parsed));
	record =
		col->data + col->polygon_offset + (size_t)index * POLYGON_BYTES;
	parsed.material = mh_read_u16le(record);

	if (!read_plane(record + POLYGON_PLANE, parsed.plane))
		return 0;

	for (i = 0; i < MAX_BOUNDARIES; i++) {
		reference = mh_read_u16le(
			record + POLYGON_BOUNDARY_REFERENCES + i * 2);

		if (reference == 0)
			break; /* Remaining words may be uninitialized. */

		/* Decrement before extracting the orientation bit. */
		reference--;
		boundary = reference & REFERENCE_INDEX_MASK;

		if (boundary >= col->boundary_count ||
			!read_plane(col->data + col->boundary_offset +
					(size_t)boundary * PLANE_BYTES,
				parsed.boundaries[i]))
			return 0;

		if ((reference & REVERSED_PLANE_BIT) != 0) {
			for (j = 0; j < 4; j++)
				parsed.boundaries[i][j] =
					-parsed.boundaries[i][j];
		}
	}

	if (i < 3)
		return 0;

	parsed.boundary_count = i;
	*polygon = parsed;

	return 1;
}

static int
valid_tree_depth(const struct mh_col *col, unsigned long reference,
	unsigned int depth, unsigned char *checked_depth)
{
	unsigned int i;

	if (reference == 0 || (reference & LIST_REFERENCE_BIT) != 0)
		return 1;

	if (depth == 0)
		return 0;

	if (checked_depth[reference] != 0 && checked_depth[reference] <= depth)
		return 1;

	for (i = 0; i < NODE_CHILDREN; i++) {
		if (!valid_tree_depth(col,
			mh_read_u16le(col->data + col->node_offset +
				(size_t)reference * NODE_BYTES +
				i * INDEX_BYTES),
			depth - 1, checked_depth))
			return 0;
	}

	checked_depth[reference] = (unsigned char)depth;

	return 1;
}

static int
valid_reference(const struct mh_col *col, unsigned long reference,
	const unsigned char *list_starts)
{
	if (reference == 0)
		return 1;

	if ((reference & LIST_REFERENCE_BIT) != 0) {
		reference &= REFERENCE_INDEX_MASK;
		return reference < col->list_units &&
			list_starts[reference] != 0;
	}

	return reference < col->node_count;
}

static enum mh_error_code
validate_octree(const struct mh_col *col, struct mh_error *error_out)
{
	const unsigned char *data;
	unsigned char *list_starts, *checked_depth;
	unsigned long n, count, reference;
	unsigned int i;
	size_t offset;
	enum mh_error_code status;

	data = col->data;
	list_starts = (unsigned char *)calloc((size_t)col->list_units, 1);
	checked_depth = (unsigned char *)calloc((size_t)col->node_count, 1);
	status = mh_error_set(error_out, MH_ERROR_MEMORY,
		"out of memory validating COL tree");

	if (list_starts == NULL || checked_depth == NULL)
		goto done;

	offset = LIST_UNIT_BYTES;
	status = mh_error_set(
		error_out, MH_ERROR_INVALID, "invalid COL polygon list");

	while (offset < (size_t)col->list_units * LIST_UNIT_BYTES) {
		count = mh_read_u16le(data + col->list_offset + offset);

		if (count == 0 ||
			count > ((size_t)col->list_units * LIST_UNIT_BYTES -
					offset - INDEX_BYTES) /
					INDEX_BYTES)
			goto done;

		list_starts[offset / LIST_UNIT_BYTES] = 1;

		for (n = 0; n < count; n++) {
			if (mh_read_u16le(data + col->list_offset + offset +
				INDEX_BYTES + (size_t)n * INDEX_BYTES) >=
				col->polygon_count)
				goto done;
		}

		offset += (size_t)((count + 2) / 2) * LIST_UNIT_BYTES;
	}

	status = mh_error_set(
		error_out, MH_ERROR_INVALID, "invalid COL octree reference");

	for (n = 1; n < col->node_count; n++) {
		for (i = 0; i < NODE_CHILDREN; i++) {
			reference = mh_read_u16le(data + col->node_offset +
				(size_t)n * NODE_BYTES + i * INDEX_BYTES);

			if (!valid_reference(col, reference, list_starts))
				goto done;
		}
	}

	for (offset = 0; offset < col->root_count; offset++) {
		reference = mh_read_u16le(
			data + HEADER_BYTES + offset * INDEX_BYTES);

		if (!valid_reference(col, reference, list_starts))
			goto done;
	}

	status = mh_error_set(error_out, MH_ERROR_INVALID,
		"cyclic or excessively deep COL octree");

	for (n = 1; n < col->node_count; n++) {
		if (!valid_tree_depth(col, n, col->depth, checked_depth))
			goto done;
	}

	for (offset = 0; offset < col->root_count; offset++) {
		if (!valid_tree_depth(col,
			mh_read_u16le(
				data + HEADER_BYTES + offset * INDEX_BYTES),
			col->depth, checked_depth))
			goto done;
	}

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

done:
	free(list_starts);
	free(checked_depth);

	return status;
}

enum mh_error_code
mh_col_parse(const unsigned char *data, size_t size, struct mh_col *col,
	struct mh_error *error_out)
{
	struct mh_col parsed;
	struct mh_col_polygon polygon;
	unsigned long n;
	unsigned long root_width;
	unsigned int i;
	size_t offset;
	double boundary_plane[4];
	enum mh_error_code status;

	if (data == NULL || col == NULL || size < HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "COL header is truncated");

	memset(&parsed, 0, sizeof(parsed));
	parsed.data = data;
	parsed.node_offset = (size_t)mh_read_u16le(data + HEADER_NODE_OFFSET);
	parsed.depth = (unsigned int)mh_read_u16le(data + HEADER_DEPTH);

	if (parsed.depth > MAX_DEPTH)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported COL octree depth");

	root_width = 1UL << parsed.depth;
	parsed.root_count = 1;

	for (i = 0; i < 3; i++) {
		parsed.dimensions[i] =
			mh_read_u16le(data + HEADER_DIMENSIONS + i * 2);
		parsed.roots[i] = parsed.dimensions[i] >> parsed.depth;

		if (parsed.roots[i] == 0 ||
			parsed.dimensions[i] % root_width != 0 ||
			parsed.root_count > (size - HEADER_BYTES) /
					INDEX_BYTES / parsed.roots[i])
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid COL grid dimensions");

		parsed.root_count *= (size_t)parsed.roots[i];

		if (!mh_read_f32le(data + HEADER_TRANSLATION + i * 4,
			&parsed.translation[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid COL grid translation");
	}

	if (parsed.node_offset !=
		HEADER_BYTES + parsed.root_count * INDEX_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"COL grid/header span mismatch");

	if (!mh_read_f32le(data + HEADER_SCALE, &parsed.scale) ||
		!(parsed.scale > 0) ||
		!mh_read_f32le(data + HEADER_CELL_SIZE, &parsed.cell_size) ||
		!(parsed.cell_size > 0) ||
		fabs(parsed.scale * parsed.cell_size - 1.0) >
			RECIPROCAL_TOLERANCE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid COL grid scale");

	parsed.polygon_count = mh_read_u16le(data + HEADER_POLYGON_COUNT);
	parsed.node_count = mh_read_u16le(data + HEADER_NODE_COUNT);
	parsed.list_units = mh_read_u16le(data + HEADER_LIST_UNITS);

	/*
	 * Header +16 and pool element zero contain unused nonportable values.
	 */
	parsed.boundary_count = mh_read_u16le(data + HEADER_BOUNDARY_COUNT);

	if (parsed.node_count == 0 || parsed.list_units == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"COL sentinel pools are missing");

	offset = parsed.node_offset;

	if (parsed.node_count > (size - offset) / NODE_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"COL nodes extend outside file");

	offset += (size_t)parsed.node_count * NODE_BYTES;
	parsed.list_offset = offset;

	if (parsed.list_units > (size - offset) / LIST_UNIT_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"COL lists extend outside file");

	offset += (size_t)parsed.list_units * LIST_UNIT_BYTES;
	parsed.polygon_offset = offset;

	if (parsed.polygon_count > (size - offset) / POLYGON_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"COL polygons extend outside file");

	offset += (size_t)parsed.polygon_count * POLYGON_BYTES;
	parsed.boundary_offset = offset;

	if (parsed.boundary_count > (size - offset) / PLANE_BYTES ||
		(size_t)parsed.boundary_count * PLANE_BYTES != size - offset)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"COL boundary plane span mismatch");

	for (n = 0; n < parsed.boundary_count; n++) {
		if (!read_plane(data + offset + (size_t)n * PLANE_BYTES,
			boundary_plane))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid COL boundary plane");
	}

	for (n = 0; n < parsed.polygon_count; n++) {
		if (!mh_col_polygon(&parsed, n, &polygon))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid COL polygon or boundary reference");
	}

	status = validate_octree(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*col = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const unsigned char *
mh_col_cell(const struct mh_col *col, const unsigned long cell[3],
	unsigned long *count)
{
	unsigned int depth;
	unsigned long reference;
	unsigned long child;
	size_t root;
	const unsigned char *list;

	*count = 0;

	if (cell[0] >= col->dimensions[0] || cell[1] >= col->dimensions[1] ||
		cell[2] >= col->dimensions[2])
		return NULL;

	depth = col->depth;
	root = (((size_t)(cell[0] >> depth) * col->roots[1]) +
		(cell[1] >> depth)) *
			col->roots[2] +
		(cell[2] >> depth);
	reference =
		mh_read_u16le(col->data + HEADER_BYTES + root * INDEX_BYTES);

	while (reference != 0 && (reference & LIST_REFERENCE_BIT) == 0) {
		depth--;
		child = (((cell[0] >> depth) & 1) << 2) |
			(((cell[1] >> depth) & 1) << 1) |
			((cell[2] >> depth) & 1);
		reference = mh_read_u16le(col->data + col->node_offset +
			(size_t)reference * NODE_BYTES + child * INDEX_BYTES);
	}

	if (reference == 0)
		return NULL;

	list = col->data + col->list_offset +
		(size_t)(reference & REFERENCE_INDEX_MASK) * LIST_UNIT_BYTES;
	*count = mh_read_u16le(list);

	return list + INDEX_BYTES;
}

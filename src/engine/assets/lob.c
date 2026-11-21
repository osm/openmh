#include <stdlib.h>
#include <string.h>

#include "engine/assets/lob.h"
#include "engine/assets/tbl.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define COUNT_LIMIT 65536
#define COORDINATE_LIMIT 1000000

enum {
	HEADER_BYTES = 8,
	HEADER_EDGE_COUNT = 4,
	EDGE_SECOND_POINT = 4,
	POINT_BYTES = 32,
	EDGE_BYTES = 8
};

static enum mh_error_code
parse_lob(const unsigned char *data, size_t size, struct mh_wireframe *output,
	struct mh_error *error_out)
{
	struct mh_wireframe result;
	unsigned long point_count, edge_count, first, second;
	unsigned int i, axis;
	size_t offset;
	double value;

	if (size < HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short LOB header");

	point_count = mh_read_u32le(data);
	edge_count = mh_read_u32le(data + HEADER_EDGE_COUNT);

	if (point_count == 0 || point_count > COUNT_LIMIT ||
		edge_count > COUNT_LIMIT ||
		point_count > (size - HEADER_BYTES) / POINT_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LOB counts");

	offset = HEADER_BYTES + (size_t)point_count * POINT_BYTES;

	if (edge_count > (size - offset) / EDGE_BYTES ||
		offset + (size_t)edge_count * EDGE_BYTES != size)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LOB edge span");

	memset(&result, 0, sizeof(result));
	result.point_count = (unsigned int)point_count;
	result.edge_count = (unsigned int)edge_count;
	result.points = (struct mh_wireframe_point *)malloc(
		(size_t)point_count * sizeof(*result.points));
	result.edges = edge_count == 0
		? NULL
		: (struct mh_wireframe_edge *)malloc(
			(size_t)edge_count * sizeof(*result.edges));

	if (result.points == NULL ||
		(edge_count != 0 && result.edges == NULL)) {
		mh_wireframe_free(&result);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading LOB");
	}

	for (i = 0; i < point_count; i++) {
		for (axis = 0; axis < 3; axis++) {
			if (!mh_read_f32le(data + HEADER_BYTES +
					(size_t)i * POINT_BYTES + axis * 4,
				&value) ||
				value < -COORDINATE_LIMIT ||
				value > COORDINATE_LIMIT) {
				mh_wireframe_free(&result);
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid LOB coordinate");
			}

			result.points[i].xyz[axis] = (float)value;
		}
	}

	for (i = 0; i < edge_count; i++) {
		first = mh_read_u32le(data + offset + (size_t)i * EDGE_BYTES);
		second = mh_read_u32le(data + offset + (size_t)i * EDGE_BYTES +
			EDGE_SECOND_POINT);

		if (first >= point_count || second >= point_count) {
			mh_wireframe_free(&result);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid LOB edge index");
		}

		result.edges[i].first = (unsigned int)first;
		result.edges[i].second = (unsigned int)second;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_wireframe_lob_decode(const unsigned char *data, size_t size,
	struct mh_wireframe *output, struct mh_error *error_out)
{
	unsigned char *unpacked;
	size_t unpacked_size;
	enum mh_error_code status;

	if (data == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LOB input");

	if (size < 3 || memcmp(data, "TBL", 3) != 0)
		return parse_lob(data, size, output, error_out);

	status = mh_tbl_resource_decode(
		data, size, NULL, &unpacked, &unpacked_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = parse_lob(unpacked, unpacked_size, output, error_out);
	free(unpacked);

	return status;
}

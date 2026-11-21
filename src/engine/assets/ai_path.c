#include <stdlib.h>
#include <string.h>

#include "engine/assets/ai_path.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define VALUE_LIMIT 1e6
#define WIDTH_LIMIT 1000
#define DIRECTION_SQUARED_MINIMUM .99
#define DIRECTION_SQUARED_MAXIMUM 1.01

enum {
	COUNT_BYTES = 4,
	POINT_BYTES = 36,
	SECTION_BYTES = 12,
	POINT_SECTION = 32,
	SECTION_POINT_COUNT = 4,
	SECTION_RADIUS = 8
};

enum point_value {
	POINT_X,
	POINT_Z,
	WIDTH_NEGATIVE,
	WIDTH_POSITIVE,
	DIRECTION_X,
	DIRECTION_Y,
	DIRECTION_Z,
	RADIUS,
	POINT_VALUE_COUNT
};

static int
read_point(const unsigned char *record, unsigned long section_count,
	struct mh_ai_point *point)
{
	double values[POINT_VALUE_COUNT], direction_length_squared;
	unsigned long section_index;
	unsigned int i;

	for (i = 0; i < POINT_VALUE_COUNT; i++) {
		if (!mh_read_f32le(record + i * 4, &values[i]) ||
			values[i] < -VALUE_LIMIT || values[i] > VALUE_LIMIT)
			return 0;
	}

	section_index = mh_read_u32le(record + POINT_SECTION);

	if (section_index >= section_count || values[WIDTH_NEGATIVE] < 0 ||
		values[WIDTH_NEGATIVE] > WIDTH_LIMIT ||
		values[WIDTH_POSITIVE] < 0 ||
		values[WIDTH_POSITIVE] > WIDTH_LIMIT)
		return 0;

	direction_length_squared = values[DIRECTION_X] * values[DIRECTION_X] +
		values[DIRECTION_Y] * values[DIRECTION_Y] +
		values[DIRECTION_Z] * values[DIRECTION_Z];

	if (direction_length_squared < DIRECTION_SQUARED_MINIMUM ||
		direction_length_squared > DIRECTION_SQUARED_MAXIMUM)
		return 0;

	point->x = values[POINT_X];
	point->z = values[POINT_Z];
	point->width[0] = values[WIDTH_NEGATIVE];
	point->width[1] = values[WIDTH_POSITIVE];

	for (i = 0; i < 3; i++)
		point->direction[i] = values[i + DIRECTION_X];

	point->radius = values[RADIUS];
	point->section = (size_t)section_index;

	return 1;
}

static int
read_sections(const unsigned char *records, struct mh_ai_path *path)
{
	const unsigned char *record;
	unsigned long tuning_index, point_count;
	size_t i;
	double radius;

	for (i = 0; i < path->section_count; i++) {
		record = records + i * SECTION_BYTES;
		tuning_index = mh_read_u32le(record);
		point_count = mh_read_u32le(record + SECTION_POINT_COUNT);

		if (tuning_index != i || point_count > path->count ||
			!mh_read_f32le(record + SECTION_RADIUS, &radius) ||
			radius < -VALUE_LIMIT || radius > VALUE_LIMIT)
			return 0;

		path->sections[i].count = (size_t)point_count;
		path->sections[i].radius = radius;
	}

	return 1;
}

enum mh_error_code
mh_ai_path_dat_decode(const unsigned char *data, size_t size,
	struct mh_ai_path *output, struct mh_error *error_out)
{
	struct mh_ai_path path;
	unsigned long point_count, section_count;
	size_t sections_offset, i;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size < 2 * COUNT_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated AI header");

	point_count = mh_read_u32le(data);

	if (point_count < 2 || point_count > MH_AI_MAX_POINTS ||
		point_count > (size - 2 * COUNT_BYTES) / POINT_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid AI point count");

	sections_offset = COUNT_BYTES + (size_t)point_count * POINT_BYTES;
	section_count = mh_read_u32le(data + sections_offset);

	if (section_count == 0 || section_count > MH_AI_MAX_SECTIONS ||
		section_count > (size - sections_offset - COUNT_BYTES) /
				SECTION_BYTES ||
		size - sections_offset - COUNT_BYTES !=
			section_count * SECTION_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI section table size");

	if (sizeof(*path.points) > (size_t)-1 / point_count ||
		sizeof(*path.sections) > (size_t)-1 / section_count)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "AI allocation overflow");

	memset(&path, 0, sizeof(path));
	path.count = (size_t)point_count;
	path.section_count = (size_t)section_count;
	path.points =
		(struct mh_ai_point *)malloc(path.count * sizeof(*path.points));
	path.sections = (struct mh_ai_section *)malloc(
		path.section_count * sizeof(*path.sections));
	status = mh_error_set(error_out, MH_ERROR_INVALID, "invalid AI point");

	if (path.points == NULL || path.sections == NULL) {
		status = mh_error_set(
			error_out, MH_ERROR_MEMORY, "AI allocation failed");
		goto fail;
	}

	for (i = 0; i < path.count; i++)
		if (!read_point(data + COUNT_BYTES + i * POINT_BYTES,
			section_count, &path.points[i]))
			goto fail;

	status = mh_error_set(
		error_out, MH_ERROR_INVALID, "invalid AI section metadata");

	if (!read_sections(data + sections_offset + COUNT_BYTES, &path))
		goto fail;

	*output = path;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

fail:
	mh_ai_path_free(&path);

	return status;
}

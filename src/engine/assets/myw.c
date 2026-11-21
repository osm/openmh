#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/myw.h"
#include "engine/assets/myw_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum link_state {
	LINK_UNSEEN,
	LINK_VISITING,
	LINK_COMPLETE
};

struct model_slice {
	unsigned int	world_block, model_block;
	unsigned int	offset_field, count_field;
	size_t		stride;
};

/* Primitive slice lengths come from walking their variable-size records. */
static const struct model_slice model_slices[SLICE_COUNT] = {
	{MH_MYW_VERTICES, MH_MYO_VERTICES, 0x8c, 0x80, 12},
	{MH_MYW_PLANES, MH_MYO_PLANES, 0x90, 0x84, 16},
	{MH_MYW_NORMALS, MH_MYO_NORMALS, 0x94, 0x82, 12},
	{MH_MYW_UV, MH_MYO_UV, 0x98, 0x88, 1},
	{MH_MYW_PRIMITIVES, MH_MYO_PRIMITIVES, MODEL_PRIMITIVE_OFFSET,
		MODEL_PRIMITIVE_COUNT, 0}};

int
mh_myw_directional(
	const struct mh_myw *world, struct mh_directional_light *light)
{
	struct mh_directional_light parsed;
	unsigned int i;

	if (world == NULL || world->data == NULL || light == NULL ||
		!mh_read_f32le(world->data + HEADER_DIRECTIONAL_INTENSITY,
			&parsed.intensity))
		return 0;

	for (i = 0; i < 3; i++) {
		parsed.color[i] = world->data[HEADER_DIRECTIONAL_COLOR + i];

		if (!mh_read_f32le(
			world->data + HEADER_DIRECTIONAL_DIRECTION + i * 4,
			&parsed.direction[i]))
			return 0;
	}

	*light = parsed;

	return 1;
}

int
mh_myw_light(const struct mh_myw *world, unsigned long index,
	struct mh_point_light *light)
{
	struct mh_point_light parsed;
	const unsigned char *record;
	unsigned int axis;

	if (index >= world->blocks[MH_MYW_LIGHTS].count)
		return 0;

	record = world->data + world->blocks[MH_MYW_LIGHTS].offset +
		(size_t)index * LIGHT_BYTES;

	if (record[0] != POINT_LIGHT_TYPE)
		return 0;

	for (axis = 0; axis < 3; axis++) {
		if (!mh_read_f32le(record + LIGHT_POSITION + axis * 4,
			&parsed.position[axis]) ||
			!mh_read_f32le(record + LIGHT_COLOR + axis * 4,
				&parsed.color[axis]))
			return 0;
	}

	if (!mh_read_f32le(record + LIGHT_INTENSITY, &parsed.intensity) ||
		!mh_read_f32le(record + LIGHT_RADIUS, &parsed.radius))
		return 0;

	*light = parsed;

	return 1;
}

int
mh_myw_halo(const struct mh_myw *world, unsigned long index,
	struct mh_geometry_halo *halo)
{
	struct mh_geometry_halo parsed;
	const unsigned char *record;
	unsigned int endpoint;
	unsigned int axis;

	if (index >= world->blocks[MH_MYW_AUXILIARY].count)
		return 0;

	record = world->data + world->blocks[MH_MYW_AUXILIARY].offset +
		(size_t)index * HALO_BYTES;

	if (record[0] != HALO_TYPE)
		return 0;

	memset(&parsed, 0, sizeof(parsed));
	parsed.resource = mh_read_u32le(record + HALO_RESOURCE);

	for (endpoint = 0; endpoint < 2; endpoint++) {
		for (axis = 0; axis < 3; axis++) {
			if (!mh_read_f32le(record + HALO_ENDPOINTS +
					endpoint * 12 + axis * 4,
				&parsed.endpoints[endpoint][axis]))
				return 0;
		}
	}

	memcpy(parsed.color, record + HALO_COLOR, 3);
	*halo = parsed;

	return 1;
}

enum mh_error_code
mh_myw_lights_begin(const struct mh_myw *world, const double position[3],
	unsigned long *cursor, struct mh_error *error_out)
{
	double x;
	double z;
	size_t cell;
	unsigned int i;

	if (world == NULL || position == NULL || cursor == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid light query");

	for (i = 0; i < 3; i++) {
		if (!(position[i] >= -FLT_MAX && position[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid light position");
	}

	x = (position[0] - world->grid_minimum[0]) / world->cell_size[0];
	z = (position[2] - world->grid_minimum[2]) / world->cell_size[1];
	*cursor = MH_MYW_NO_LIGHT;

	/*
	 * Original truncation toward zero includes the (-1,0) interval. Check
	 * bounds before integer conversion, including arbitrarily distant
	 * points.
	 */
	if (x <= -1 || z <= -1 || x >= world->width || z >= world->height)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	cell = (z < 0 ? 0 : (size_t)z) * (size_t)world->width +
		(x < 0 ? 0 : (size_t)x);
	*cursor = mh_read_u32le(world->data + world->grid_offset +
		cell * CELL_BYTES + CELL_LIGHT_HEAD);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_myw_light_head(
	const struct mh_myw *world, size_t cell, unsigned long *cursor)
{
	if (world == NULL || cursor == NULL || cell >= world->cell_count)
		return 0;

	*cursor = mh_read_u32le(world->data + world->grid_offset +
		cell * CELL_BYTES + CELL_LIGHT_HEAD);

	return 1;
}

int
mh_myw_light_next(const struct mh_myw *world, unsigned long *cursor,
	struct mh_point_light *light)
{
	const unsigned char *node;

	if (*cursor == MH_MYW_NO_LIGHT)
		return 0;

	node = world->data + world->blocks[MH_MYW_LINKS].offset +
		(size_t)*cursor;

	if (!mh_myw_light(world, mh_read_u32le(node) / LIGHT_BYTES, light))
		return 0;

	*cursor = mh_read_u32le(node + LINK_NEXT);

	return 1;
}

static enum mh_error_code
validate_effects(const struct mh_myw *world, struct mh_error *error_out)
{
	const struct mh_myo_block *links;
	const unsigned char *node;
	struct mh_point_light light;
	struct mh_geometry_halo halo;
	unsigned char *link_states;
	unsigned long index;
	unsigned long light_offset;
	unsigned long next_offset;
	unsigned long link_offset;

	for (index = 0; index < world->blocks[MH_MYW_LIGHTS].count; index++) {
		if (!mh_myw_light(world, index, &light))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"invalid or unsupported MYW point light");
	}

	for (index = 0; index < world->blocks[MH_MYW_AUXILIARY].count;
		index++) {
		if (!mh_myw_halo(world, index, &halo))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"invalid or unsupported MYW halo record");
	}

	links = &world->blocks[MH_MYW_LINKS];

	for (index = 0; index < links->count; index++) {
		node = world->data + links->offset + (size_t)index * LINK_BYTES;
		light_offset = mh_read_u32le(node);
		next_offset = mh_read_u32le(node + LINK_NEXT);

		if (light_offset % LIGHT_BYTES != 0 ||
			light_offset / LIGHT_BYTES >=
				world->blocks[MH_MYW_LIGHTS].count ||
			(next_offset != MH_MYW_NO_LIGHT &&
				(next_offset % LINK_BYTES != 0 ||
					next_offset / LINK_BYTES >=
						links->count))) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW light-list link");
		}
	}

	if (links->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	link_states = (unsigned char *)calloc((size_t)links->count, 1);

	if (link_states == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory checking MYW light lists");

	/*
	 * Linear-time cycle check, including unreferenced nodes/shared tails.
	 */
	for (index = 0; index < links->count; index++) {
		link_offset = index * LINK_BYTES;

		while (link_offset != MH_MYW_NO_LIGHT &&
			link_states[link_offset / LINK_BYTES] == LINK_UNSEEN) {
			link_states[link_offset / LINK_BYTES] = LINK_VISITING;
			link_offset =
				mh_read_u32le(world->data + links->offset +
					(size_t)link_offset + LINK_NEXT);
		}

		if (link_offset != MH_MYW_NO_LIGHT &&
			link_states[link_offset / LINK_BYTES] ==
				LINK_VISITING) {
			free(link_states);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"cyclic MYW light list");
		}

		link_offset = index * LINK_BYTES;

		while (link_offset != MH_MYW_NO_LIGHT &&
			link_states[link_offset / LINK_BYTES] ==
				LINK_VISITING) {
			link_states[link_offset / LINK_BYTES] = LINK_COMPLETE;
			link_offset =
				mh_read_u32le(world->data + links->offset +
					(size_t)link_offset + LINK_NEXT);
		}
	}

	free(link_states);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Resolve model slices within the world data. */
static enum mh_error_code
read_slice(const struct mh_myw *world, unsigned int pool, unsigned long offset,
	unsigned long count, size_t stride, struct mh_myo_block *block,
	struct mh_error *error_out)
{
	const struct mh_myo_block *source;

	source = &world->blocks[pool];

	if (offset % stride != 0 || offset > source->size ||
		count > (source->size - offset) / stride) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYW model slice extends outside its pool");
	}

	block->count = count;
	block->size = (size_t)count * stride;
	block->offset = source->offset + (size_t)offset;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_myw_model(const struct mh_myw *world, unsigned long index,
	struct mh_myo *model, struct mh_error *error_out)
{
	struct mh_myo parsed;
	struct mh_geometry_face face;
	const struct model_slice *mapping;
	const unsigned char *record;
	const struct mh_myo_block *pool;
	struct mh_myo_block *faces;
	enum mh_error_code status;
	unsigned long count;
	unsigned long offset;
	unsigned long n;
	unsigned int i;
	unsigned int axis;
	size_t cursor;
	double position[3];

	pool = &world->blocks[MH_MYW_MODELS];

	if (index >= pool->count)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYW model index is out of range");

	record = world->data + pool->offset + (size_t)index * MODEL_BYTES;
	memset(&parsed, 0, sizeof(parsed));
	parsed.data = world->data;

	/* A pool view, not a standalone MYO file version. */
	parsed.version = 0;

	for (i = 0; i < FIXED_SLICE_COUNT; i++) {
		mapping = &model_slices[i];
		count = mapping->model_block == MH_MYO_UV
			? mh_read_u32le(record + mapping->count_field)
			: mh_read_u16le(record + mapping->count_field);

		status = read_slice(world, mapping->world_block,
			mh_read_u32le(record + mapping->offset_field), count,
			mapping->stride, &parsed.blocks[mapping->model_block],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	parsed.blocks[MH_MYO_TEXTURE_REFS] = world->blocks[MH_MYW_TEXTURES];
	parsed.blocks[MH_MYO_TEXTURE_NAMES] = world->blocks[MH_MYW_NAMES];
	pool = &world->blocks[MH_MYW_PRIMITIVES];
	offset = mh_read_u32le(record + MODEL_PRIMITIVE_OFFSET);

	if (offset > pool->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYW primitive offset is outside its pool");

	faces = &parsed.blocks[MH_MYO_PRIMITIVES];
	faces->offset = pool->offset + (size_t)offset;
	faces->size = pool->size - (size_t)offset;
	faces->count = mh_read_u16le(record + MODEL_PRIMITIVE_COUNT);
	cursor = 0;

	for (n = 0; n < faces->count; n++) {
		status = mh_myo_next_face(&parsed, &cursor, &face, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (face.count == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYW primitive slice is truncated");

		parsed.primitive_types[face.type]++;
	}

	faces->size = cursor;

	for (n = 0; n < parsed.blocks[MH_MYO_VERTICES].count; n++) {
		if (!mh_myo_vertex(&parsed, n, position)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYW vertex contains non-finite coordinates");
		}

		for (axis = 0; axis < 3; axis++) {
			if (n == 0 || position[axis] < parsed.minimum[axis]) {
				parsed.minimum[axis] = position[axis];
			}

			if (n == 0 || position[axis] > parsed.maximum[axis]) {
				parsed.maximum[axis] = position[axis];
			}
		}
	}

	*model = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_myw_cell(const struct mh_myw *world, size_t index, struct mh_myw_cell *cell)
{
	const unsigned char *record;
	struct mh_myw_cell result;

	if (world == NULL || cell == NULL || index >= world->cell_count)
		return 0;

	record = world->data + world->grid_offset + index * CELL_BYTES;
	result.model = mh_read_u32le(record);

	if (result.model != MH_MYW_NO_MODEL)
		result.model /= MODEL_BYTES;

	result.halo_count = mh_read_u32le(record + CELL_HALO_COUNT);
	result.halo_first = result.halo_count == 0
		? 0
		: mh_read_u32le(record + CELL_HALO_OFFSET) / HALO_BYTES;
	*cell = result;

	return 1;
}

int
mh_myw_model_bounds(const struct mh_myw *world, unsigned long index,
	float center[3], float *radius)
{
	const unsigned char *record;
	double values[4];
	unsigned int i;

	if (world == NULL || center == NULL || radius == NULL ||
		index >= world->blocks[MH_MYW_MODELS].count)
		return 0;

	record = world->data + world->blocks[MH_MYW_MODELS].offset +
		(size_t)index * MODEL_BYTES + MODEL_SPHERE;

	for (i = 0; i < 4; i++)
		if (!mh_read_f32le(record + i * 4, values + i))
			return 0;

	if (values[3] < 0)
		return 0;

	for (i = 0; i < 3; i++)
		center[i] = (float)values[i];

	*radius = (float)values[3];

	return 1;
}

static enum mh_error_code
validate_grid(const struct mh_myw *world, struct mh_error *error_out)
{
	const struct mh_myo_block *block;
	const unsigned char *record;
	unsigned long offset, halo_count;
	size_t cell;

	for (cell = 0; cell < world->cell_count; cell++) {
		record = world->data + world->grid_offset + cell * CELL_BYTES;
		offset = mh_read_u32le(record);

		if (offset != MH_MYW_NO_MODEL &&
			(offset % MODEL_BYTES != 0 ||
				offset / MODEL_BYTES >=
					world->blocks[MH_MYW_MODELS].count)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW grid model reference");
		}

		offset = mh_read_u32le(record + CELL_LIGHT_HEAD);

		if (offset != MH_MYW_NO_LIGHT &&
			(offset % LINK_BYTES != 0 ||
				offset / LINK_BYTES >=
					world->blocks[MH_MYW_LINKS].count)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW grid light-list reference");
		}

		halo_count = mh_read_u32le(record + CELL_HALO_COUNT);

		if (halo_count != 0) {
			block = &world->blocks[MH_MYW_AUXILIARY];
			offset = mh_read_u32le(record + CELL_HALO_OFFSET);

			if (offset % HALO_BYTES != 0 || offset > block->size ||
				halo_count >
					(block->size - offset) / HALO_BYTES) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid MYW grid auxiliary span");
			}
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_texture_names(const struct mh_myw *world, struct mh_error *error_out)
{
	const struct mh_myo_block *block;
	const unsigned char *name_end;
	unsigned long n;
	size_t cursor, end;

	block = &world->blocks[MH_MYW_NAMES];
	cursor = block->offset;
	end = cursor + block->size;

	for (n = 0; n < block->count; n++) {
		name_end = (const unsigned char *)memchr(
			world->data + cursor, 0, end - cursor);

		if (name_end == NULL || name_end == world->data + cursor) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW texture name");
		}

		cursor = (size_t)(name_end - world->data) + 1;
	}

	if (cursor != end)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYW texture name count mismatch");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_models(struct mh_myw *world, struct mh_error *error_out)
{
	struct mh_myo model;
	const unsigned char *record;
	enum mh_error_code status;
	unsigned long model_index, total_faces;
	unsigned int i, axis;
	size_t used_bytes[SLICE_COUNT];
	int have_bounds;
	float model_center[3], model_radius;

	memset(used_bytes, 0, sizeof(used_bytes));
	total_faces = 0;
	have_bounds = 0;

	for (model_index = 0; model_index < world->blocks[MH_MYW_MODELS].count;
		model_index++) {
		record = world->data + world->blocks[MH_MYW_MODELS].offset +
			(size_t)model_index * MODEL_BYTES;

		if (!mh_myw_model_bounds(
			world, model_index, model_center, &model_radius))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW model sphere");

		for (i = 0; i < SLICE_COUNT; i++) {
			if (mh_read_u32le(
				record + model_slices[i].offset_field) !=
				used_bytes[i]) {
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"non-contiguous MYW model slices are not supported");
			}
		}

		status = mh_myw_model(world, model_index, &model, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < SLICE_COUNT; i++)
			used_bytes[i] +=
				model.blocks[model_slices[i].model_block].size;

		if (model.blocks[MH_MYO_PRIMITIVES].count >
			world->blocks[MH_MYW_PRIMITIVES].count - total_faces) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"MYW face count exceeds world total");
		}

		total_faces += model.blocks[MH_MYO_PRIMITIVES].count;

		for (i = 0; i < MH_MYO_PRIMITIVE_TYPES; i++)
			world->primitive_types[i] += model.primitive_types[i];

		if (model.blocks[MH_MYO_VERTICES].count != 0) {
			for (axis = 0; axis < 3; axis++) {
				if (!have_bounds ||
					model.minimum[axis] <
						world->minimum[axis]) {
					world->minimum[axis] =
						model.minimum[axis];
				}

				if (!have_bounds ||
					model.maximum[axis] >
						world->maximum[axis]) {
					world->maximum[axis] =
						model.maximum[axis];
				}
			}

			have_bounds = 1;
		}
	}

	for (i = 0; i < SLICE_COUNT; i++) {
		if (used_bytes[i] !=
			world->blocks[model_slices[i].world_block].size)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYW pool coverage mismatch");
	}

	if (total_faces != world->blocks[MH_MYW_PRIMITIVES].count) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYW world face count mismatch");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_myw_parse(const unsigned char *data, size_t size, struct mh_myw *world,
	struct mh_error *error_out)
{
	static const size_t strides[MH_MYW_BLOCK_COUNT] = {MODEL_BYTES, 12, 24,
		0, 16, 12, 12, 0, LIGHT_BYTES, LINK_BYTES, 0, HALO_BYTES};

	struct mh_myw parsed;
	struct mh_myo_block *block;
	enum mh_error_code status;
	unsigned long offset;
	unsigned long block_size;
	unsigned int i;

	if (data == NULL || world == NULL || size < HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "MYW header is truncated");

	if (memcmp(data, "!WYM", 4) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported MYW signature");

	if (mh_read_u32le(data + HEADER_FILE_SIZE) != size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYW file size does not match header");

	memset(&parsed, 0, sizeof(parsed));
	parsed.data = data;

	for (i = 0; i < MH_MYW_BLOCK_COUNT; i++) {
		block = &parsed.blocks[i];
		block->count = mh_read_u32le(
			data + HEADER_BLOCK_COUNT + i * BLOCK_BYTES);
		block_size = mh_read_u32le(
			data + HEADER_BLOCK_SIZE + i * BLOCK_BYTES);
		offset = mh_read_u32le(
			data + HEADER_BLOCK_OFFSET + i * BLOCK_BYTES);

		if (offset < HEADER_BYTES || offset > size ||
			block_size > size - offset) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"MYW block extends outside the file");
		}

		block->size = (size_t)block_size;
		block->offset = (size_t)offset;

		if (strides[i] != 0 &&
			(block_size % strides[i] != 0 ||
				block->count != block_size / strides[i])) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYW pool count/stride mismatch");
		}
	}

	parsed.width = mh_read_u32le(data + HEADER_GRID_WIDTH);
	parsed.height = mh_read_u32le(data + HEADER_GRID_HEIGHT);
	offset = mh_read_u32le(data + HEADER_GRID_OFFSET);

	if (parsed.width == 0 || parsed.height == 0 || offset < HEADER_BYTES ||
		offset > size ||
		parsed.width > (size - offset) / CELL_BYTES / parsed.height) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYW grid extends outside the file");
	}

	parsed.grid_offset = (size_t)offset;
	parsed.cell_count = (size_t)parsed.width * (size_t)parsed.height;

	for (i = 0; i < 2; i++) {
		if (!mh_read_f64le(data + HEADER_CELL_SIZE + i * 8,
			&parsed.cell_size[i]) ||
			!(parsed.cell_size[i] >= FLT_MIN &&
				parsed.cell_size[i] <= FLT_MAX)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW cell spacing");
		}

		parsed.cell_size[i] = (float)parsed.cell_size[i];
	}

	for (i = 0; i < 3; i++) {
		if (!mh_read_f32le(data + HEADER_GRID_MINIMUM + i * 4,
			&parsed.grid_minimum[i])) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYW grid origin");
		}
	}

	status = validate_effects(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_grid(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_texture_names(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_models(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*world = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

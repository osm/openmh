#include <string.h>

#include "engine/assets/myo.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define SIGNATURE_V1 0x4d594f21UL
#define SIGNATURE_V2 0x4d594f32UL
#define RADIUS_LIMIT 1e9
#define FLOAT_EXPONENT_MASK 0x7f800000UL
#define FLOAT_FRACTION_MASK 0x007fffffUL

enum {
	HEADER_BYTES = 124,
	HEADER_AUXILIARY_COUNT = 0x5c,
	HEADER_AUXILIARY_OFFSET = 0x64,
	HEADER_RADIUS = 0x68,
	BLOCK_DESCRIPTOR_BYTES = 12,
	HEADER_BLOCK_COUNT = 4,
	HEADER_BLOCK_SIZE = 8,
	HEADER_BLOCK_OFFSET = 12,
	FACE_PLANE = 2,
	VECTOR_BYTES = 12,
	PLANE_BYTES = 16,
	TEXTURE_REF_BYTES = 12,
	HALO_BYTES = 120,
	HALO_TYPE = 2,
	HALO_RESOURCE = 4,
	HALO_ENDPOINTS = 8,
	HALO_COLOR = 0x30
};

/* Face record sizes by type. */
static const unsigned int primitive_sizes[MH_MYO_PRIMITIVE_TYPES] = {
	16, 20, 24, 24, 24, 32, 32, 36, 20, 24};

const char *
mh_myo_texture_name(const struct mh_myo *model, unsigned long index)
{
	const char *name;
	unsigned long i;

	if (index >= model->blocks[MH_MYO_TEXTURE_NAMES].count) {
		return NULL;
	}

	name = (const char *)model->data +
		model->blocks[MH_MYO_TEXTURE_NAMES].offset;

	for (i = 0; i < index; i++) {
		name += strlen(name) + 1;
	}

	return name;
}

static enum mh_error_code
read_face_texture(const struct mh_myo *model, const unsigned char *record,
	struct mh_geometry_face *face, struct mh_error *error_out)
{
	const struct mh_myo_block *texture_refs, *uv;
	unsigned long offset;
	unsigned int texture_offset, uv_offset, corner, axis;

	texture_refs = &model->blocks[MH_MYO_TEXTURE_REFS];
	texture_offset = face->type < 4 ? 20 : face->type == 6 ? 28 : 32;
	uv_offset = face->type < 4 ? 18 : face->type == 6 ? 26 : 30;
	offset = mh_read_u32le(record + texture_offset);

	if (offset % TEXTURE_REF_BYTES != 0 || offset > texture_refs->size ||
		texture_refs->size - offset < TEXTURE_REF_BYTES ||
		texture_refs->count != texture_refs->size / TEXTURE_REF_BYTES ||
		texture_refs->size % TEXTURE_REF_BYTES != 0) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MYO texture indirection");
	}

	face->texture = mh_read_u32le(
		model->data + texture_refs->offset + (size_t)offset);

	if (face->texture >= model->blocks[MH_MYO_TEXTURE_NAMES].count) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MYO texture name index");
	}

	uv = &model->blocks[MH_MYO_UV];
	offset = mh_read_u16le(record + uv_offset);

	if (offset > uv->size || uv->size - offset < face->count * 2) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYO face UV coordinates extend outside their block");
	}

	for (corner = 0; corner < face->count; corner++) {
		for (axis = 0; axis < 2; axis++) {
			face->corners[corner].uv[axis] = 0.5 +
				model->data[uv->offset + (size_t)offset +
					corner * 2 + axis];
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
collapsed_corners(const struct mh_geometry_face *face)
{
	unsigned int i, j, distinct;
	const double *a, *b;

	distinct = 0;

	for (i = 0; i < face->count; i++) {
		a = face->corners[i].position;

		for (j = 0; j < i; j++) {
			b = face->corners[j].position;

			if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2])
				break;
		}

		if (j == i && ++distinct == 3)
			return 0;
	}

	return 1;
}

static int
read_face_value(const unsigned char *data, int collapsed,
	struct mh_geometry_face *face, double *value)
{
	unsigned long bits;

	if (mh_read_f32le(data, value))
		return 1;

	bits = mh_read_u32le(data);

	if (!collapsed || (bits & FLOAT_EXPONENT_MASK) != FLOAT_EXPONENT_MASK ||
		(bits & FLOAT_FRACTION_MASK) == 0)
		return 0;

	/* Dominator has repeated vertices with undefined planes and normals. */
	face->degenerate = 1;
	*value = 0;

	return 1;
}

static int
read_face_normal(const unsigned char *data, int collapsed,
	struct mh_geometry_face *face, double normal[3])
{
	unsigned long bits;
	unsigned int axis;
	int undefined;

	undefined = 1;

	for (axis = 0; axis < 3; axis++) {
		bits = mh_read_u32le(data + axis * 4);

		if ((bits & FLOAT_EXPONENT_MASK) != FLOAT_EXPONENT_MASK ||
			(bits & FLOAT_FRACTION_MASK) == 0)
			undefined = 0;
	}

	/*
	 * Dominator shares an all-NaN normal between collapsed and valid faces.
	 * Repair valid faces with their plane direction.
	 */
	if (undefined && !collapsed) {
		if (face->plane[0] == 0 && face->plane[1] == 0 &&
			face->plane[2] == 0)
			return 0;

		memcpy(normal, face->plane, 3 * sizeof(*normal));

		return 1;
	}

	for (axis = 0; axis < 3; axis++) {
		if (!read_face_value(
			data + axis * 4, collapsed, face, &normal[axis]))
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_myo_next_face(const struct mh_myo *model, size_t *cursor,
	struct mh_geometry_face *face, struct mh_error *error_out)
{
	struct mh_geometry_face parsed;
	enum mh_error_code status;
	const struct mh_myo_block *primitives;
	const struct mh_myo_block *normals;
	const unsigned char *record;
	const unsigned char *plane;
	const unsigned char *normal;
	unsigned long index;
	unsigned int corner;
	unsigned int axis;
	unsigned int normal_offset;
	unsigned int vertex_offset;
	int collapsed;
	size_t remaining;

	memset(&parsed, 0, sizeof(parsed));
	primitives = &model->blocks[MH_MYO_PRIMITIVES];

	if (*cursor > primitives->size) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MYO primitive cursor is outside its block");
	}

	remaining = primitives->size - *cursor;

	if (remaining == 0) {
		*face = parsed;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	record = model->data + primitives->offset + *cursor;
	parsed.type = record[0];

	if (parsed.type >= MH_MYO_PRIMITIVE_TYPES ||
		primitive_sizes[parsed.type] > remaining) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid MYO face record");
	}

	parsed.flags = record[1];
	parsed.count = 3 + (parsed.type & 1);
	memcpy(parsed.color, record + 4, 3);
	parsed.texture = MH_GEOMETRY_NO_TEXTURE;
	vertex_offset =
		parsed.type >= 4 && parsed.type <= 7 ? 4 + parsed.count * 4 : 8;

	for (corner = 0; corner < parsed.count; corner++) {
		index = mh_read_u16le(record + vertex_offset + corner * 2);

		if (!mh_myo_vertex(
			model, index, parsed.corners[corner].position))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYO face vertex index");
	}

	collapsed = collapsed_corners(&parsed);
	index = mh_read_u16le(record + FACE_PLANE);

	if (index >= model->blocks[MH_MYO_PLANES].count ||
		model->blocks[MH_MYO_PLANES].count !=
			model->blocks[MH_MYO_PLANES].size / PLANE_BYTES ||
		model->blocks[MH_MYO_PLANES].size % PLANE_BYTES != 0) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MYO face plane index or plane block");
	}

	plane = model->data + model->blocks[MH_MYO_PLANES].offset +
		(size_t)index * PLANE_BYTES;

	for (axis = 0; axis < 4; axis++) {
		if (!read_face_value(plane + axis * 4, collapsed, &parsed,
			&parsed.plane[axis])) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO face plane contains a non-finite coordinate");
		}
	}

	normals = &model->blocks[MH_MYO_NORMALS];

	if (normals->count != normals->size / VECTOR_BYTES ||
		normals->size % VECTOR_BYTES != 0) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MYO normal block");
	}

	for (corner = 0; corner < parsed.count; corner++) {
		memcpy(parsed.corners[corner].color,
			record + 4 + (vertex_offset == 8 ? 0 : corner * 4), 3);
		normal_offset = vertex_offset + parsed.count * 2;

		if (parsed.type >= 8) {
			normal_offset += corner * 2;
		}

		index = mh_read_u16le(record + normal_offset);

		if (index >= normals->count) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MYO face normal index");
		}

		normal = model->data + normals->offset +
			(size_t)index * VECTOR_BYTES;

		if (!read_face_normal(normal, collapsed, &parsed,
			parsed.corners[corner].normal)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO normal contains a non-finite coordinate");
		}
	}

	if (parsed.type == 2 || parsed.type == 3 || parsed.type == 6 ||
		parsed.type == 7) {
		status = read_face_texture(model, record, &parsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*cursor += primitive_sizes[parsed.type];
	*face = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_myo_vertex(
	const struct mh_myo *model, unsigned long index, double position[3])
{
	const unsigned char *record;
	int axis;

	if (index >= model->blocks[MH_MYO_VERTICES].count) {
		return 0;
	}

	record = model->data + model->blocks[MH_MYO_VERTICES].offset +
		(size_t)index * VECTOR_BYTES;

	for (axis = 0; axis < 3; axis++) {
		if (!mh_read_f32le(record + axis * 4, &position[axis])) {
			return 0;
		}
	}

	return 1;
}

int
mh_myo_halo(const struct mh_myo *model, unsigned long index,
	struct mh_geometry_halo *halo)
{
	struct mh_geometry_halo result;
	const unsigned char *record;
	unsigned int endpoint, axis;

	if (model == NULL || halo == NULL || index >= model->auxiliary_count)
		return 0;

	record = model->data + model->auxiliary_offset +
		(size_t)index * HALO_BYTES;

	if (mh_read_u32le(record) != HALO_TYPE)
		return 0;

	memset(&result, 0, sizeof(result));
	result.resource = mh_read_u32le(record + HALO_RESOURCE);

	for (endpoint = 0; endpoint < 2; endpoint++)
		for (axis = 0; axis < 3; axis++)
			if (!mh_read_f32le(record + HALO_ENDPOINTS +
					(endpoint * 3 + axis) * 4,
				&result.endpoints[endpoint][axis]))
				return 0;

	memcpy(result.color, record + HALO_COLOR, 3);
	*halo = result;

	return 1;
}

static enum mh_error_code
validate_primitives(struct mh_myo *model, struct mh_error *error_out)
{
	const struct mh_myo_block *block;
	unsigned long index;
	unsigned int type;
	size_t cursor, end;

	block = &model->blocks[MH_MYO_PRIMITIVES];
	cursor = block->offset;
	end = cursor + block->size;

	for (index = 0; index < block->count; index++) {
		if (cursor == end) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO primitive block is truncated");
		}

		type = model->data[cursor];

		if (type >= MH_MYO_PRIMITIVE_TYPES) {
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported MYO primitive type");
		}

		if (primitive_sizes[type] > end - cursor) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO primitive record is truncated");
		}

		model->primitive_types[type]++;
		cursor += primitive_sizes[type];
	}

	if (cursor != end) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYO primitive count does not match its block size");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_texture_names(const struct mh_myo *model, struct mh_error *error_out)
{
	const struct mh_myo_block *block;
	const unsigned char *name_end;
	unsigned long index;
	size_t cursor, end;

	block = &model->blocks[MH_MYO_TEXTURE_NAMES];
	cursor = block->offset;
	end = cursor + block->size;

	for (index = 0; index < block->count; index++) {
		name_end = (const unsigned char *)memchr(
			model->data + cursor, 0, end - cursor);

		if (name_end == NULL) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO texture name is not terminated");
		}

		cursor = (size_t)(name_end - model->data) + 1;
	}

	if (cursor != end) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYO texture-name count does not match its block size");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_myo_parse(const unsigned char *data, size_t size, struct mh_myo *model,
	struct mh_error *error_out)
{
	struct mh_myo parsed;
	struct mh_myo_block *block;
	struct mh_geometry_halo halo;
	unsigned long offset;
	unsigned long block_size;
	unsigned long index;
	unsigned long magic;
	int i;
	int axis;
	double position[3];
	enum mh_error_code status;

	if (data == NULL || model == NULL || size < HEADER_BYTES) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYO header is truncated or missing");
	}

	memset(&parsed, 0, sizeof(parsed));
	magic = mh_read_u32le(data);

	if (magic == SIGNATURE_V1) {
		parsed.version = 1;
	} else if (magic == SIGNATURE_V2) {
		parsed.version = 2;
	} else {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported MYO signature");
	}

	parsed.data = data;

	if (!mh_read_f32le(data + HEADER_RADIUS, &parsed.radius) ||
		parsed.radius < 0 || parsed.radius > RADIUS_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MYO bounding sphere radius");

	/*
	 * These offsets are relative to the file base. Decode and bound them
	 * before converting to size_t values.
	 */
	for (i = 0; i < MH_MYO_BLOCK_COUNT; i++) {
		block = &parsed.blocks[i];
		block->count = mh_read_u32le(
			data + HEADER_BLOCK_COUNT + i * BLOCK_DESCRIPTOR_BYTES);
		block_size = mh_read_u32le(
			data + HEADER_BLOCK_SIZE + i * BLOCK_DESCRIPTOR_BYTES);
		offset = mh_read_u32le(data + HEADER_BLOCK_OFFSET +
			i * BLOCK_DESCRIPTOR_BYTES);

		if (offset < HEADER_BYTES || offset > size ||
			block_size > size - offset) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"MYO block extends outside the file");
		}

		block->offset = (size_t)offset;
		block->size = (size_t)block_size;
	}

	/* All extracted variants store 12-byte xyz vertex records. */
	block = &parsed.blocks[MH_MYO_VERTICES];

	if (block->size % VECTOR_BYTES != 0 ||
		block->count != block->size / VECTOR_BYTES) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MYO vertex count does not match its block size");
	}

	for (index = 0; index < block->count; index++) {
		if (!mh_myo_vertex(&parsed, index, position)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MYO vertex contains a non-finite coordinate");
		}

		for (axis = 0; axis < 3; axis++) {
			if (index == 0 ||
				position[axis] < parsed.minimum[axis]) {
				parsed.minimum[axis] = position[axis];
			}

			if (index == 0 ||
				position[axis] > parsed.maximum[axis]) {
				parsed.maximum[axis] = position[axis];
			}
		}
	}

	status = validate_primitives(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_texture_names(&parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Version 1 leaves these slots uninitialized. Version 2 resolves
	 * auxiliary records with a 0x78-byte stride.
	 */
	if (parsed.version == 2) {
		parsed.auxiliary_count =
			mh_read_u32le(data + HEADER_AUXILIARY_COUNT);
		offset = mh_read_u32le(data + HEADER_AUXILIARY_OFFSET);

		if (offset < HEADER_BYTES || offset > size ||
			parsed.auxiliary_count > (size - offset) / HALO_BYTES) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"MYO auxiliary records extend outside the file");
		}

		parsed.auxiliary_offset = (size_t)offset;

		for (index = 0; index < parsed.auxiliary_count; index++)
			if (!mh_myo_halo(&parsed, index, &halo))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid MYO halo type or coordinates");
	}

	*model = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_retro.h"
#include "engine/assets/retro.h"
#include "engine/assets/retro_layout.h"
#include "engine/core/binary.h"
#include "engine/retro/parts.h"

#define FLOAT_LIMIT 1000

enum {
	WORD_BYTES = 4,
	VECTOR_BYTES = MH_RETRO_VECTOR_BYTES,
	MOTION_BYTES = MH_RETRO_MOTION_VECTORS * VECTOR_BYTES
};

const char *
mh_retro_mhretro_filename(enum mh_retro_part part)
{
	static const char *const names[] = {MH_ASSET_RETRO_MESHES_NAME,
		MH_ASSET_RETRO_ANIMATION_NAME, MH_ASSET_RETRO_PALETTE_NAME,
		MH_ASSET_RETRO_STORY_NAME};

	return (unsigned int)part < MH_RETRO_PART_COUNT ? names[part] : NULL;
}

void
mh_retro_mhretro_free(struct mh_retro_files *files)
{
	unsigned int i;

	for (i = 0; i < MH_RETRO_PART_COUNT; i++)
		free(files->parts[i].data);

	memset(files, 0, sizeof(*files));
}

static int
read_floats(const unsigned char *data, unsigned int count, float *output)
{
	unsigned int i;
	double value;

	for (i = 0; i < count; i++) {
		if (!mh_read_f32le(data + i * WORD_BYTES, &value) ||
			value < -FLOAT_LIMIT || value > FLOAT_LIMIT)
			return 0;

		output[i] = (float)value;
	}

	return 1;
}

static int
face_normal(struct mh_retro_mesh *mesh, struct mh_retro_face *face)
{
	float first_edge[3], second_edge[3], cross[3];
	double length_squared;
	long double inverse_length;
	unsigned int i, j, k;

	for (i = 0; i < 3; i++) {
		first_edge[i] =
			(float)((long double)mesh->points[face->indices[1]][i] -
				mesh->points[face->indices[0]][i]);
		second_edge[i] =
			(float)((long double)mesh->points[face->indices[2]][i] -
				mesh->points[face->indices[0]][i]);
	}

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		cross[i] = (float)((long double)first_edge[j] * second_edge[k] -
			(long double)first_edge[k] * second_edge[j]);
	}

	length_squared = (double)(((long double)cross[1] * cross[1] +
					(long double)cross[0] * cross[0]) +
		(long double)cross[2] * cross[2]);

	if (!(length_squared > 0))
		return 0;

	/* Round the squared length to double before taking its square root. */
	inverse_length = 1 / (long double)sqrt(length_squared);

	for (i = 0; i < 3; i++)
		face->normal[i] = (float)(cross[i] * inverse_length);

	return 1;
}

static enum mh_error_code
read_mesh(const unsigned char *data, size_t size, size_t *used,
	struct mh_retro_mesh *mesh, struct mh_error *error_out)
{
	struct mh_retro_face *face;
	size_t offset;
	unsigned long value;
	unsigned int i, j;

	if (size < WORD_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing retro vertex count");

	value = mh_read_u32le(data);

	if (value < 3 || value > MH_RETRO_MAX_POINTS ||
		size - WORD_BYTES < value * VECTOR_BYTES + WORD_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid retro vertices");

	mesh->point_count = (unsigned int)value;
	offset = WORD_BYTES;

	for (i = 0; i < mesh->point_count; i++) {
		if (!read_floats(data + offset, 3, mesh->points[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro vertex");

		offset += VECTOR_BYTES;
	}

	value = mh_read_u32le(data + offset);
	offset += WORD_BYTES;

	if (value == 0 || value > MH_RETRO_MAX_FACES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro face count");

	mesh->face_count = (unsigned int)value;

	for (i = 0; i < mesh->face_count; i++) {
		if (size - offset < WORD_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated retro polygon");

		face = &mesh->faces[i];
		value = mh_read_u32le(data + offset);
		offset += WORD_BYTES;

		if (value < 3 || value > 4 ||
			size - offset < (value + 1) * WORD_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro polygon size");

		face->count = (unsigned int)value;

		for (j = 0; j < face->count; j++) {
			value = mh_read_u32le(data + offset);
			offset += WORD_BYTES;

			if (value >= mesh->point_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro polygon index");

			face->indices[j] = (unsigned int)value;
		}

		value = mh_read_u32le(data + offset);
		offset += WORD_BYTES;

		if (value < 1 || value > MH_RETRO_PALETTE_COUNT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro polygon color");

		face->palette_index = (unsigned int)value - 1;

		if (!face_normal(mesh, face))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"degenerate retro polygon");
	}

	*used = offset;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_meshes(const unsigned char *data, size_t size,
	union mh_retro_part_data *assets, struct mh_error *error_out)
{
	size_t offset, used;
	unsigned int i;
	enum mh_error_code status;

	if (size < MH_RETRO_HEADER_BYTES ||
		memcmp(data, MH_RETRO_MESH_SIGNATURE,
			MH_RETRO_SIGNATURE_BYTES) != 0 ||
		mh_read_u32le(data + MH_RETRO_COUNT_OFFSET) !=
			MH_RETRO_MESH_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro mesh header");

	offset = MH_RETRO_HEADER_BYTES;
	used = 0;

	for (i = 0; i < MH_RETRO_MESH_COUNT; i++) {
		status = read_mesh(data + offset, size - offset, &used,
			&assets->meshes[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		offset += used;
	}

	if (offset != size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"retro meshes have trailing data");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_animation(const unsigned char *data, size_t size,
	union mh_retro_part_data *assets, struct mh_error *error_out)
{
	unsigned int i, j;

	if (size !=
			MH_RETRO_ANIMATION_HEADER_BYTES +
				MH_RETRO_INSTANCE_COUNT * MOTION_BYTES ||
		memcmp(data, MH_RETRO_ANIMATION_SIGNATURE,
			MH_RETRO_SIGNATURE_BYTES) != 0 ||
		mh_read_u32le(data + MH_RETRO_COUNT_OFFSET) !=
			MH_RETRO_INSTANCE_COUNT ||
		mh_read_u32le(data + MH_RETRO_VECTOR_COUNT_OFFSET) !=
			MH_RETRO_MOTION_VECTORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro animation header");

	data += MH_RETRO_ANIMATION_HEADER_BYTES;

	for (i = 0; i < MH_RETRO_INSTANCE_COUNT; i++)

		for (j = 0; j < MH_RETRO_MOTION_VECTORS; j++) {
			if (!read_floats(data, 3, assets->motion[i][j]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro motion");

			data += VECTOR_BYTES;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_palette(const unsigned char *data, size_t size,
	union mh_retro_part_data *assets, struct mh_error *error_out)
{
	unsigned int i, j;

	if (size !=
			MH_RETRO_HEADER_BYTES +
				MH_RETRO_PALETTE_COUNT * VECTOR_BYTES ||
		memcmp(data, MH_RETRO_PALETTE_SIGNATURE,
			MH_RETRO_SIGNATURE_BYTES) != 0 ||
		mh_read_u32le(data + MH_RETRO_COUNT_OFFSET) !=
			MH_RETRO_PALETTE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro palette header");

	data += MH_RETRO_HEADER_BYTES;

	for (i = 0; i < MH_RETRO_PALETTE_COUNT; i++) {

		if (!read_floats(data, 3, assets->palette[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro palette");

		for (j = 0; j < 3; j++)
			if (assets->palette[i][j] < 0 ||
				assets->palette[i][j] > 255)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro palette range");

		data += VECTOR_BYTES;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_story(const unsigned char *data, size_t size,
	union mh_retro_part_data *assets, struct mh_error *error_out)
{
	const unsigned char *end;
	size_t length, consumed;
	unsigned int i;

	for (i = 0; i < MH_RETRO_LINES; i++) {
		end = memchr(data, '\n', size);

		if (end == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing retro story line");

		length = (size_t)(end - data);
		consumed = length + 1;

		if (length != 0 && data[length - 1] == '\r')
			length--;

		if (length >= MH_RETRO_LINE_BYTES ||
			memchr(data, 0, length) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro story line");

		memcpy(assets->text[i], data, length);
		data += consumed;
		size -= consumed;
	}

	if (size != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"retro story has extra lines");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_part(enum mh_retro_part part, const unsigned char *data, size_t size,
	union mh_retro_part_data *assets, struct mh_error *error_out)
{
	if (data == NULL || size > MH_RETRO_FILE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing or oversized retro file");

	switch (part) {
	case MH_RETRO_PART_MESHES:
		return read_meshes(data, size, assets, error_out);
	case MH_RETRO_PART_ANIMATION:
		return read_animation(data, size, assets, error_out);
	case MH_RETRO_PART_PALETTE:
		return read_palette(data, size, assets, error_out);
	case MH_RETRO_PART_STORY:
		return read_story(data, size, assets, error_out);
	default:
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown retro file kind");
	}
}

enum mh_error_code
mh_retro_mhretro_decode_part(enum mh_retro_part part, const unsigned char *data,
	size_t size, union mh_retro_part_data *output,
	struct mh_error *error_out)
{
	union mh_retro_part_data parsed;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing retro part output");

	memset(&parsed, 0, sizeof(parsed));
	status = read_part(part, data, size, &parsed, error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;

	return status;
}

enum mh_error_code
mh_retro_mhretro_decode(const struct mh_retro_files *files,
	struct mh_retro_assets *output, struct mh_error *error_out)
{
	struct mh_retro_assets parsed;
	union mh_retro_part_data part;
	unsigned int i;
	enum mh_error_code status;

	if (files == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing retro resources");

	memset(&parsed, 0, sizeof(parsed));

	for (i = 0; i < MH_RETRO_PART_COUNT; i++) {
		status = mh_retro_mhretro_decode_part((enum mh_retro_part)i,
			files->parts[i].data, files->parts[i].size, &part,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_retro_part_store(&parsed, (enum mh_retro_part)i, &part);
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_mhretro_check(enum mh_retro_part part, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	union mh_retro_part_data parsed;

	return mh_retro_mhretro_decode_part(
		part, data, size, &parsed, error_out);
}

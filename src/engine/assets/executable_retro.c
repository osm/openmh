#include <stdlib.h>
#include <string.h>

#include "engine/assets/executable_identity.h"
#include "engine/assets/executable_retro.h"
#include "engine/assets/retro_layout.h"
#include "engine/core/binary.h"
#include "engine/core/sha256.h"

struct retro_layout {
	const struct mh_executable_identity	*identity;
	size_t	meshes, mesh_bytes, animation, palette, story;
};

/* File spans for embedded intro assets. */
static const struct retro_layout retail_12 = {
	&mh_executable_retail_12,
	0xc9a88UL, /* Mesh offset. */
	0x14f0UL,  /* Mesh byte count. */
	0xcaf78UL, /* Animation offset. */
	0xcb02cUL, /* Palette offset. */
	0xcb060UL, /* Story offset. */
};

static const struct retro_layout retail_30 = {
	&mh_executable_retail_30,
	0xe719cUL, /* Mesh offset. */
	0x14f0UL,  /* Mesh byte count. */
	0xe868cUL, /* Animation offset. */
	0xe8740UL, /* Palette offset. */
	0xe8774UL, /* Story offset. */
};

static const struct retro_layout retail_31 = {
	&mh_executable_retail_31,
	0xe3b04UL, /* Mesh offset. */
	0x14f0UL,  /* Mesh byte count. */
	0xe4ff4UL, /* Animation offset. */
	0xe50a8UL, /* Palette offset. */
	0xe50dcUL, /* Story offset. */
};

static const struct retro_layout *const layouts[] = {
	&retail_12,
	&retail_30,
	&retail_31,
};

enum {
	MOTION_BYTES = MH_RETRO_INSTANCE_COUNT * MH_RETRO_MOTION_VECTORS *
		MH_RETRO_VECTOR_BYTES,
	PALETTE_BYTES = MH_RETRO_PALETTE_COUNT * MH_RETRO_VECTOR_BYTES,
	STORY_BYTES = MH_RETRO_LINES * MH_RETRO_LINE_BYTES
};

size_t
mh_retro_executable_limit(void)
{
	size_t i, limit;

	limit = 0;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (layouts[i]->identity->size > limit)
			limit = layouts[i]->identity->size;

	return limit;
}

static enum mh_error_code
identify_executable(const struct mh_file *file,
	const struct retro_layout **output, struct mh_error *error_out)
{
	struct mh_sha256 hash;
	unsigned char digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE];
	size_t i;

	if (file == NULL || file->data == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing executable for retro assets");

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (file->size == layouts[i]->identity->size)
			break;

	if (i == sizeof(layouts) / sizeof(layouts[0]))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported executable size for retro assets");

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, file->data, file->size);
	mh_sha256_finish(&hash, digest);
	mh_sha256_text(digest, text);

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (file->size == layouts[i]->identity->size &&
			strcmp(text, layouts[i]->identity->sha256) == 0) {
			*output = layouts[i];
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"unrecognized executable for embedded retro assets");
}

static int
contains(const struct mh_file *file, size_t offset, size_t bytes)
{
	return offset <= file->size && bytes <= file->size - offset;
}

static enum mh_error_code
copy_part(const unsigned char *data, size_t size, const char *magic,
	unsigned int count, unsigned int header, struct mh_file *file,
	struct mh_error *error_out)
{
	file->data = malloc(header + size);

	if (file->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory extracting retro assets");

	file->size = header + size;
	memcpy(file->data, magic, MH_RETRO_SIGNATURE_BYTES);
	mh_write_u32le(file->data + MH_RETRO_COUNT_OFFSET, count);

	if (header == MH_RETRO_ANIMATION_HEADER_BYTES)
		mh_write_u32le(file->data + MH_RETRO_VECTOR_COUNT_OFFSET,
			MH_RETRO_MOTION_VECTORS);

	memcpy(file->data + header, data, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
extract_story(const unsigned char *data, struct mh_file *file,
	struct mh_error *error_out)
{
	const unsigned char *end;
	size_t length;
	unsigned int i;

	file->data = malloc(MH_RETRO_LINES * MH_RETRO_LINE_BYTES);

	if (file->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory extracting retro story");

	file->size = 0;

	for (i = 0; i < MH_RETRO_LINES; i++) {
		end = memchr(data, 0, MH_RETRO_LINE_BYTES);

		if (end == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated embedded retro story line");

		length = (size_t)(end - data);
		memcpy(file->data + file->size, data, length);
		file->size += length;
		file->data[file->size++] = '\n';
		data += MH_RETRO_LINE_BYTES;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_executable_extract(const struct mh_file *executable,
	struct mh_retro_files *output, struct mh_error *error_out)
{
	struct mh_retro_files files;
	const unsigned char *data;
	const struct retro_layout *layout;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing retail retro resource output");

	layout = NULL;
	status = identify_executable(executable, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!contains(executable, layout->meshes, layout->mesh_bytes) ||
		!contains(executable, layout->animation, MOTION_BYTES) ||
		!contains(executable, layout->palette, PALETTE_BYTES) ||
		!contains(executable, layout->story, STORY_BYTES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated embedded retro resources");

	data = executable->data;
	memset(&files, 0, sizeof(files));
	status = copy_part(data + layout->meshes, layout->mesh_bytes,
		MH_RETRO_MESH_SIGNATURE, MH_RETRO_MESH_COUNT,
		MH_RETRO_HEADER_BYTES, &files.parts[MH_RETRO_PART_MESHES],
		error_out);

	if (status == MH_ERROR_NONE)
		status = copy_part(data + layout->animation, MOTION_BYTES,
			MH_RETRO_ANIMATION_SIGNATURE, MH_RETRO_INSTANCE_COUNT,
			MH_RETRO_ANIMATION_HEADER_BYTES,
			&files.parts[MH_RETRO_PART_ANIMATION], error_out);

	if (status == MH_ERROR_NONE)
		status = copy_part(data + layout->palette, PALETTE_BYTES,
			MH_RETRO_PALETTE_SIGNATURE, MH_RETRO_PALETTE_COUNT,
			MH_RETRO_HEADER_BYTES,
			&files.parts[MH_RETRO_PART_PALETTE], error_out);

	if (status == MH_ERROR_NONE)
		status = extract_story(data + layout->story,
			&files.parts[MH_RETRO_PART_STORY], error_out);

	if (status != MH_ERROR_NONE) {
		mh_retro_mhretro_free(&files);
		return status;
	}

	*output = files;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

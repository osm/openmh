#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/assets/font_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

enum {
	ATLAS_SIDE = 16,
	SIGNED_OFFSET_LIMIT = 128,
	WIDE_QUIET_BYTE = 6,
	DIMENSION_LIMIT = 64,
	PROGRAM_LIMIT = 4096
};

static enum mh_error_code
decode_glyph(const unsigned char *data, size_t end, size_t cursor,
	unsigned int character, struct mh_font_program *font,
	struct mh_error *error_out)
{
	size_t byte_offset;
	unsigned int y, x, command_bytes, span_bytes, i;

	if (end - cursor > PROGRAM_LIMIT)
		end = cursor + PROGRAM_LIMIT;

	y = 0;

	while (cursor < end) {
		if (data[cursor] == FNT_END) {
			/*
			 * The loader patches this displacement. Some files
			 * retain a stale value; it is deliberately neither
			 * trusted nor followed.
			 */
			if (end - cursor < FNT_END_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"truncated FNT terminator");

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (end - cursor < FNT_ROW_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated FNT command");

		if (data[cursor] == FNT_ROW &&
			(data[cursor + 1] == FNT_ROW_SINGLE ||
				data[cursor + 1] == FNT_ROW_DOUBLE)) {
			y += data[cursor + 1] == FNT_ROW_SINGLE ? 1 : 2;

			if (y >= font->height)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"FNT row exceeds glyph");

			cursor += FNT_ROW_BYTES;
			continue;
		}

		command_bytes = FNT_SPAN_BYTES;
		span_bytes = 0;

		if (data[cursor] == FNT_SPAN_WORD_PREFIX) {
			command_bytes = FNT_WORD_SPAN_BYTES;

			if (end - cursor >= FNT_WORD_SPAN_BYTES &&
				data[cursor + 1] == FNT_SPAN_DWORD &&
				data[cursor + 2] == FNT_SPAN_ADDRESS)
				span_bytes = 2;
		} else if (data[cursor + 1] == FNT_SPAN_ADDRESS) {
			if (data[cursor] == FNT_SPAN_BYTE)
				span_bytes = 1;

			if (data[cursor] == FNT_SPAN_DWORD)
				span_bytes = 4;
		} else if (data[cursor] == FNT_SPAN_QWORD &&
			data[cursor + 1] == FNT_SPAN_QWORD_ADDRESS)
			span_bytes = 8;

		if (span_bytes == 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported FNT drawing command");

		if (end - cursor < command_bytes)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated FNT span");

		x = data[cursor + command_bytes - 1];

		if (x >= SIGNED_OFFSET_LIMIT || x > font->advance[character] ||
			span_bytes > font->advance[character] - x)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"FNT span exceeds glyph");

		byte_offset =
			((size_t)((character / ATLAS_SIDE) * font->height + y) *
					font->width +
				(character % ATLAS_SIDE) * font->cell_width +
				x);

		for (i = 0; i < span_bytes; i++)
			font->bytes[byte_offset + i] =
				span_bytes == 8 && i == WIDE_QUIET_BYTE
				? MH_FONT_WIDE_LOW
				: (i & 1) ? MH_FONT_HIGH
					: MH_FONT_LOW;

		cursor += command_bytes;
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unterminated FNT glyph");
}

enum mh_error_code
mh_font_fnt_decode_program(const unsigned char *data, size_t size,
	struct mh_font_program *output, struct mh_error *error_out)
{
	struct mh_font_program font;
	unsigned long height, width, offset, next_offset;
	unsigned int i, j;
	size_t end;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size < FNT_TABLE_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short FNT table");

	memset(&font, 0, sizeof(font));
	height = mh_read_u32le(data);

	if (height == 0 || height > DIMENSION_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid FNT height");

	font.height = (unsigned int)height;

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		offset = mh_read_u32le(
			data + FNT_TABLE_PREFIX_BYTES + i * FNT_ENTRY_BYTES);
		width = mh_read_u32le(data + FNT_TABLE_PREFIX_BYTES +
			FNT_WIDTH_OFFSET + i * FNT_ENTRY_BYTES);

		if (offset < FNT_TABLE_BYTES || offset >= size ||
			width > DIMENSION_LIMIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid FNT glyph table");

		font.advance[i] = (unsigned int)width;

		if (width > font.cell_width)
			font.cell_width = (unsigned int)width;
	}

	if (font.cell_width == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty FNT font");

	font.width = font.cell_width * ATLAS_SIDE;
	font.rows = font.height * ATLAS_SIDE;
	font.bytes = calloc((size_t)font.width * font.rows, 1);

	if (font.bytes == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding FNT");

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		offset = mh_read_u32le(
			data + FNT_TABLE_PREFIX_BYTES + i * FNT_ENTRY_BYTES);
		end = size;

		for (j = 0; j < MH_FONT_GLYPHS; j++) {
			next_offset = mh_read_u32le(data +
				FNT_TABLE_PREFIX_BYTES + j * FNT_ENTRY_BYTES);

			if (next_offset > offset && next_offset < end)
				end = (size_t)next_offset;
		}

		status = decode_glyph(
			data, end, (size_t)offset, i, &font, error_out);

		if (status != MH_ERROR_NONE) {
			free(font.bytes);
			return status;
		}
	}

	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_font_fnt_decode(const unsigned char *data, size_t size,
	struct mh_font *output, struct mh_error *error_out)
{
	struct mh_font_program program;
	struct mh_font font;
	size_t i, count;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short FNT table");

	status = mh_font_fnt_decode_program(data, size, &program, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&font, 0, sizeof(font));
	font.image.width = program.width;
	font.image.height = program.rows;
	font.cell_width = program.cell_width;
	font.height = program.height;
	memcpy(font.advance, program.advance, sizeof(font.advance));
	count = (size_t)program.width * program.rows;
	font.image.rgba = calloc(count, 4);

	if (font.image.rgba == NULL) {
		free(program.bytes);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding FNT");
	}

	for (i = 0; i < count; i++)
		if (program.bytes[i] != MH_FONT_SKIP)
			memset(font.image.rgba + i * 4, 255, 4);

	free(program.bytes);
	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

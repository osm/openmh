#include <stdlib.h>
#include <string.h>

#include "engine/assets/spr.h"
#include "engine/assets/spr_format.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum {
	OP_RETURN = 0xc3,
	OP_ADD_ORIGIN = 0x05,
	OP_STORE_BYTE = 0xc6,
	OP_STORE_DWORD = 0xc7,
	OP_WORD_PREFIX = 0x66,
	ADDRESS_ORIGIN_DISP8 = 0x40,
	OP_SAVE_ORIGIN = 0x50,
	OP_RESTORE_ORIGIN = 0x58,
	OP_ADD_REGISTER = 0x01,
	ADD_ROW_PITCH = 0xf8,
	SIGNED_DISPLACEMENT_LIMIT = 128,
	COLUMN_STORE_BYTES = 5
};

static enum mh_error_code
decode_indexed_row(const unsigned char *data, size_t cursor, size_t end,
	unsigned int width, unsigned char *pixels, struct mh_error *error_out)
{
	unsigned long value;
	unsigned int origin, offset, store_bytes, i;
	unsigned char opcode;

	origin = 0;

	while (cursor < end) {
		opcode = data[cursor++];

		if (opcode == OP_RETURN)
			return origin == 0
				? mh_error_set(error_out, MH_ERROR_NONE, NULL)
				: mh_error_set(error_out, MH_ERROR_INVALID,
					"SPR row leaves a shifted origin");

		if (opcode == OP_ADD_ORIGIN) {
			if (end - cursor < 4)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"short SPR origin adjustment");

			value = mh_read_u32le(data + cursor);
			cursor += 4;

			if (value & MH_U32_SIGN_BIT) {
				value = MH_U32_MAX - value + 1;

				if (value > origin)
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"SPR origin precedes row");

				origin -= (unsigned int)value;
			} else {
				if (value > width - origin)
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"SPR origin exceeds row");

				origin += (unsigned int)value;
			}

			continue;
		}

		store_bytes = opcode == OP_STORE_BYTE ? 1
			: opcode == OP_STORE_DWORD    ? 4
						: 0;

		if (opcode == OP_WORD_PREFIX) {
			if (cursor == end || data[cursor++] != OP_STORE_DWORD)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported SPR word command");

			store_bytes = 2;
		}

		if (store_bytes == 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported SPR drawing command");

		if (end - cursor < 2 + store_bytes)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"short SPR pixel span");

		if (data[cursor] != ADDRESS_ORIGIN_DISP8 ||
			data[cursor + 1] >= SIGNED_DISPLACEMENT_LIMIT)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported SPR pixel address");

		offset = data[cursor + 1];
		cursor += 2;

		if (offset > width - origin ||
			store_bytes > width - origin - offset)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"SPR span exceeds row");

		offset += origin;

		for (i = 0; i < store_bytes; i++) {
			pixels[(offset + i) * 2] = data[cursor + i];
			pixels[(offset + i) * 2 + 1] = 255;
		}

		cursor += store_bytes;
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unterminated SPR row");
}

enum mh_error_code
mh_sprite_spr_decode_indexed(const struct mh_spr *archive, unsigned int index,
	unsigned int frame, struct mh_sprite_indexed *output,
	struct mh_error *error_out)
{
	struct mh_sprite_indexed image;
	const struct mh_spr_entry *entry;
	const unsigned char *data;
	size_t frame_offset, frame_end, cursor, row_end;
	unsigned int y;
	enum mh_error_code status;

	if (archive == NULL || archive->entries == NULL || output == NULL ||
		index >= archive->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SPR frame request");

	entry = &archive->entries[index];

	if (frame >= entry->frames)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"SPR frame index out of range");

	data = archive->data + entry->offset;
	frame_offset = (size_t)mh_read_u32le(
		data + SPR_FRAME_TABLE_OFFSET + frame * SPR_OFFSET_BYTES);
	frame_end = frame + 1 < entry->frames
		? (size_t)mh_read_u32le(data + SPR_FRAME_TABLE_OFFSET +
			SPR_OFFSET_BYTES + frame * SPR_OFFSET_BYTES)
		: entry->end - entry->offset;

	image.width = entry->width;
	image.height = entry->height;
	image.pixels =
		(unsigned char *)calloc((size_t)image.width * image.height, 2);

	if (image.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding SPR");

	for (y = 0; y < entry->height; y++) {
		cursor = frame_offset +
			(size_t)mh_read_u32le(
				data + frame_offset + y * SPR_OFFSET_BYTES);
		row_end = y + 1 < entry->height ? frame_offset +
				(size_t)mh_read_u32le(data + frame_offset +
					(y + 1) * SPR_OFFSET_BYTES)
						: frame_end;

		status = decode_indexed_row(data, cursor, row_end, entry->width,
			image.pixels + (size_t)y * image.width * 2, error_out);

		if (status != MH_ERROR_NONE) {
			free(image.pixels);
			return status;
		}
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sprite_spr_decode_rgba(const struct mh_spr *archive, unsigned int index,
	unsigned int frame, struct mh_image *output, struct mh_error *error_out)
{
	struct mh_sprite_indexed indexed;
	struct mh_image image;
	size_t i, pixel_count;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SPR image output");

	status = mh_sprite_spr_decode_indexed(
		archive, index, frame, &indexed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	image.width = indexed.width;
	image.height = indexed.height;
	pixel_count = (size_t)image.width * image.height;
	image.rgba = (unsigned char *)calloc(pixel_count, 4);

	if (image.rgba == NULL) {
		free(indexed.pixels);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory coloring SPR");
	}

	for (i = 0; i < pixel_count; i++)
		if (indexed.pixels[i * 2 + 1] != 0) {
			memcpy(image.rgba + i * 4,
				archive->data + archive->palette_offset +
					(size_t)indexed.pixels[i * 2] * 3,
				3);
			image.rgba[i * 4 + 3] = 255;
		}

	free(indexed.pixels);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Each meter column saves its origin, steps down rows and writes literal
 * 16-bit pixels. Restoring the origin and returning ends the column.
 */
static enum mh_error_code
decode_column(const unsigned char *data, size_t cursor, size_t end,
	unsigned int width, unsigned int height, unsigned char *pixels,
	struct mh_error *error_out)
{
	unsigned int y;

	if (cursor == end || data[cursor++] != OP_SAVE_ORIGIN)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SPR column origin save");

	y = 0;

	while (cursor < end) {
		if (data[cursor] == OP_RESTORE_ORIGIN) {
			cursor++;
			return cursor < end && data[cursor] == OP_RETURN
				? mh_error_set(error_out, MH_ERROR_NONE, NULL)
				: mh_error_set(error_out, MH_ERROR_INVALID,
					"missing SPR column return");
		}

		if (data[cursor] == OP_ADD_REGISTER) {
			if (end - cursor < 2 ||
				data[cursor + 1] != ADD_ROW_PITCH)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported SPR column step");

			if (y == height)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"SPR column exceeds height");

			y++;
			cursor += 2;
		} else if (data[cursor] == OP_WORD_PREFIX) {
			if (end - cursor < COLUMN_STORE_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"short SPR column pixel");

			if (data[cursor + 1] != OP_STORE_DWORD ||
				data[cursor + 2] != 0)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported SPR column store");

			if (y == height)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"SPR pixel exceeds column");

			pixels[(size_t)y * width * 3] = data[cursor + 3];
			pixels[(size_t)y * width * 3 + 1] = data[cursor + 4];
			pixels[(size_t)y * width * 3 + 2] = 255;
			cursor += COLUMN_STORE_BYTES;
		} else
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported SPR column command");
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unterminated SPR column");
}

static enum mh_error_code
decode_literal_rows(const struct mh_spr *archive, unsigned int index,
	unsigned int frame, struct mh_sprite_words *image,
	struct mh_error *error_out)
{
	struct mh_sprite_indexed indexed_bytes;
	size_t pixel_count, i;
	enum mh_error_code status;

	pixel_count = (size_t)image->width * image->height;
	status = mh_sprite_spr_decode_indexed(
		archive, index, frame, &indexed_bytes, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < pixel_count; i++) {
			if (indexed_bytes.pixels[i * 4 + 1] !=
				indexed_bytes.pixels[i * 4 + 3]) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"partially written SPR color word");
				break;
			}

			image->pixels[i * 3] = indexed_bytes.pixels[i * 4];
			image->pixels[i * 3 + 1] =
				indexed_bytes.pixels[i * 4 + 2];
			image->pixels[i * 3 + 2] =
				indexed_bytes.pixels[i * 4 + 1];
		}

		free(indexed_bytes.pixels);
	}

	return status;
}

static enum mh_error_code
decode_literal_columns(const struct mh_spr *archive,
	const struct mh_spr_entry *entry, unsigned int frame,
	struct mh_sprite_words *image, struct mh_error *error_out)
{
	const unsigned char *data;
	size_t frame_offset, frame_end, cursor, program_end;
	unsigned int x;
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	data = archive->data + entry->offset;
	frame_offset = (size_t)mh_read_u32le(
		data + SPR_FRAME_TABLE_OFFSET + frame * SPR_OFFSET_BYTES);
	frame_end = frame + 1 < entry->frames
		? (size_t)mh_read_u32le(data + SPR_FRAME_TABLE_OFFSET +
			SPR_OFFSET_BYTES + frame * SPR_OFFSET_BYTES)
		: entry->end - entry->offset;

	for (x = 0; x < image->width; x++) {
		cursor = frame_offset +
			(size_t)mh_read_u32le(
				data + frame_offset + x * SPR_OFFSET_BYTES);
		program_end = x + 1 < image->width ? frame_offset +
				(size_t)mh_read_u32le(data + frame_offset +
					(x + 1) * SPR_OFFSET_BYTES)
						: frame_end;

		status = decode_column(data, cursor, program_end, image->width,
			image->height, image->pixels + (size_t)x * 3,
			error_out);

		if (status != MH_ERROR_NONE)
			break;
	}

	return status;
}

enum mh_error_code
mh_sprite_spr_decode_literal(const struct mh_spr *archive, unsigned int index,
	unsigned int frame, int columns, struct mh_sprite_words *output,
	struct mh_error *error_out)
{
	struct mh_sprite_words image;
	const struct mh_spr_entry *entry;
	size_t pixel_count;
	enum mh_error_code status;

	if (archive == NULL || archive->entries == NULL || output == NULL ||
		index >= archive->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SPR literal-color request");

	if (columns != 0 && columns != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SPR program layout");

	entry = &archive->entries[index];

	if (frame >= entry->frames)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"SPR frame index out of range");

	if (!columns && (entry->width & 1) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"odd SPR literal-color row width");

	image.width = columns ? entry->height : entry->width / 2;
	image.height = columns ? entry->width : entry->height;
	pixel_count = (size_t)image.width * image.height;
	image.pixels = (unsigned char *)calloc(pixel_count, 3);

	if (image.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding SPR colors");

	if (columns)
		status = decode_literal_columns(
			archive, entry, frame, &image, error_out);
	else
		status = decode_literal_rows(
			archive, index, frame, &image, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.pixels);
		return status;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sprite_spr_decode_literal_rgba(const struct mh_spr *archive,
	unsigned int index, unsigned int frame, int columns,
	unsigned int green_bits, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_sprite_words literal;
	struct mh_image image;
	size_t i, pixel_count;
	unsigned int value, r, g, b;
	enum mh_error_code status;

	if (output == NULL || (green_bits != 5 && green_bits != 6))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SPR color format");

	status = mh_sprite_spr_decode_literal(
		archive, index, frame, columns, &literal, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	image.width = literal.width;
	image.height = literal.height;
	pixel_count = (size_t)image.width * image.height;
	image.rgba = (unsigned char *)calloc(pixel_count, 4);

	if (image.rgba == NULL) {
		free(literal.pixels);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory coloring SPR");
	}

	for (i = 0; i < pixel_count; i++)
		if (literal.pixels[i * 3 + 2] != 0) {
			value = mh_read_u16le(literal.pixels + i * 3);
			r = (value >> (5 + green_bits)) & 31;
			g = (value >> 5) & ((1U << green_bits) - 1);
			b = value & 31;
			image.rgba[i * 4] =
				(unsigned char)((r << 3) | (r >> 2));
			image.rgba[i * 4 + 1] =
				(unsigned char)((g << (8 - green_bits)) |
					(g >> (green_bits * 2 - 8)));
			image.rgba[i * 4 + 2] =
				(unsigned char)((b << 3) | (b >> 2));
			image.rgba[i * 4 + 3] = 255;
		}

	free(literal.pixels);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

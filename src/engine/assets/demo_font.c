#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_font.h"
#include "engine/assets/font.h"
#include "engine/assets/font_layout.h"
#include "engine/core/binary.h"

enum {
	FONT_COLUMNS = 16
};

static enum mh_error_code
validate_word_pixels(
	const struct mh_font_program *font, struct mh_error *error_out)
{
	size_t i, count;
	unsigned int glyph;

	if (font->cell_width % 2 != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"word font has an odd cell width");

	for (glyph = 0; glyph < MH_FONT_GLYPHS; glyph++)
		if (font->advance[glyph] % 2 != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"word font has an odd advance");

	count = (size_t)font->width * font->rows;

	for (i = 0; i < count; i += 2)
		if ((font->bytes[i] == MH_FONT_SKIP) !=
			(font->bytes[i + 1] == MH_FONT_SKIP))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"word font has partial pixel coverage");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Emit the same bounded FNT span grammar understood by the native reader. */
static size_t
write_span(unsigned char *bytes, unsigned int x, unsigned int *count)
{
	if (*count >= 8) {
		bytes[0] = FNT_SPAN_QWORD;
		bytes[1] = FNT_SPAN_QWORD_ADDRESS;
		*count = 8;
	} else if (*count >= 4) {
		bytes[0] = FNT_SPAN_DWORD;
		bytes[1] = FNT_SPAN_ADDRESS;
		*count = 4;
	} else if (*count >= 2) {
		bytes[0] = FNT_SPAN_WORD_PREFIX;
		bytes[1] = FNT_SPAN_DWORD;
		bytes[2] = FNT_SPAN_ADDRESS;
		bytes[3] = (unsigned char)x;
		*count = 2;
		return FNT_WORD_SPAN_BYTES;
	} else {
		bytes[0] = FNT_SPAN_BYTE;
		bytes[1] = FNT_SPAN_ADDRESS;
		*count = 1;
	}

	bytes[2] = (unsigned char)x;

	return FNT_SPAN_BYTES;
}

static size_t
write_glyph(const struct mh_font_program *font, unsigned int glyph,
	unsigned char *output)
{
	const unsigned char *row;
	size_t size;
	unsigned int x, y, end, count, width;

	size = 0;
	width = font->advance[glyph] / 2;

	for (y = 0; y < font->height; y++) {
		if (y != 0) {
			output[size++] = FNT_ROW;
			output[size++] = FNT_ROW_SINGLE;
		}

		row = font->bytes +
			((size_t)(glyph / FONT_COLUMNS) * font->height + y) *
				font->width +
			(glyph % FONT_COLUMNS) * font->cell_width;
		x = 0;

		while (x < width) {
			if (row[x * 2] == MH_FONT_SKIP) {
				x++;
				continue;
			}

			end = x;

			while (end < width && row[end * 2] != MH_FONT_SKIP)
				end++;

			while (x < end) {
				count = end - x;
				size += write_span(output + size, x, &count);
				x += count;
			}
		}
	}

	output[size++] = FNT_END;
	memset(output + size, 0, FNT_END_BYTES - 1);

	return size + FNT_END_BYTES - 1;
}

enum mh_error_code
mh_font_fnt_byte_pixels(const struct mh_file *source, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_font_program font;
	struct mh_file file;
	size_t capacity;
	unsigned int glyph;
	enum mh_error_code status;

	if (source == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing font preparation input/output");

	status = mh_font_fnt_decode_program(
		source->data, source->size, &font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_word_pixels(&font, error_out);

	if (status != MH_ERROR_NONE) {
		free(font.bytes);
		return status;
	}

	/*
	 * Decoder bounds height/cell width to 64; each pixel needs at most 3
	 * bytes.
	 */
	capacity = FNT_TABLE_BYTES +
		MH_FONT_GLYPHS *
			((size_t)font.height *
					(font.cell_width / 2 * FNT_SPAN_BYTES +
						FNT_ROW_BYTES) +
				FNT_END_BYTES);

	file.data = malloc(capacity);

	if (file.data == NULL) {
		free(font.bytes);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing byte-pixel font");
	}

	mh_write_u32le(file.data, font.height);
	file.size = FNT_TABLE_BYTES;

	for (glyph = 0; glyph < MH_FONT_GLYPHS; glyph++) {
		mh_write_u32le(file.data + FNT_TABLE_PREFIX_BYTES +
				glyph * FNT_ENTRY_BYTES,
			file.size);
		mh_write_u32le(file.data + FNT_TABLE_PREFIX_BYTES +
				FNT_WIDTH_OFFSET + glyph * FNT_ENTRY_BYTES,
			font.advance[glyph] / 2);
		file.size += write_glyph(&font, glyph, file.data + file.size);
	}

	free(font.bytes);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

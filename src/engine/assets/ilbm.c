#include <stdlib.h>
#include <string.h>

#include "engine/assets/ilbm.h"
#include "engine/core/error.h"

#define PIXEL_LIMIT (16UL * 1024UL * 1024UL)

#define UNSUPPORTED_DISPLAY_MODES 0x880UL /* HAM and extra-half-brite. */

enum {
	FORM_HEADER_BYTES = 12,
	CHUNK_HEADER_BYTES = 8,
	BITMAP_HEADER_BYTES = 20,
	BITMAP_WIDTH = 0,
	BITMAP_HEIGHT = 2,
	BITMAP_PLANES = 8,
	BITMAP_MASK = 9,
	BITMAP_COMPRESSION = 10,
	BITMAP_TRANSPARENT_INDEX = 12,
	PALETTE_COLORS = 256,
	PALETTE_BYTES = PALETTE_COLORS * 3,
	BYTERUN_NOOP = 128,
	BYTERUN_REPEAT_BASE = 257,
	LASSO_PENDING_ALPHA = 128,
	MASK_NONE = 0,
	MASK_PLANE = 1,
	MASK_COLOR = 2,
	MASK_LASSO = 3
};

struct ilbm_chunks {
	const unsigned char	*bitmap_header, *palette, *body;
	size_t			palette_count, body_size;
};

static unsigned long
read_u16be(const unsigned char *p)
{
	return ((unsigned long)p[0] << 8) | p[1];
}

static unsigned long
read_u32be(const unsigned char *p)
{
	return (read_u16be(p) << 16) | read_u16be(p + 2);
}

static int
unpack_row(const unsigned char *body, size_t size, size_t *cursor,
	unsigned char *row, size_t row_size, int compression)
{
	size_t written;
	size_t count;
	unsigned int control;

	if (compression == 0) {
		if (row_size > size - *cursor) {
			return 0;
		}

		memcpy(row, body + *cursor, row_size);
		*cursor += row_size;

		return 1;
	}

	written = 0;

	while (written < row_size) {
		if (*cursor == size) {
			return 0;
		}

		control = body[(*cursor)++];

		if (control == BYTERUN_NOOP) {
			continue;
		}

		count = control < BYTERUN_NOOP ? control + 1
					: BYTERUN_REPEAT_BASE - control;

		if (count > row_size - written) {
			return 0;
		}

		if (control < BYTERUN_NOOP) {
			if (count > size - *cursor) {
				return 0;
			}

			memcpy(row + written, body + *cursor, count);
			*cursor += count;
		} else {
			if (*cursor == size) {
				return 0;
			}

			memset(row + written, body[(*cursor)++], count);
		}

		written += count;
	}

	return 1;
}

static void
enqueue_transparent(
	unsigned char *rgba, size_t *queue, size_t *tail, size_t pixel)
{
	if (rgba[pixel * 4 + 3] == LASSO_PENDING_ALPHA) {
		rgba[pixel * 4 + 3] = 0;
		queue[(*tail)++] = pixel;
	}
}

/* ILBM mskLasso: flood transparent-color pixels from the image border. */
static int
apply_lasso_mask(struct mh_image *image)
{
	size_t *queue;
	size_t head;
	size_t tail;
	size_t pixel;
	size_t count;
	size_t x;
	size_t y;

	count = (size_t)image->width * image->height;

	if (count > (size_t)-1 / sizeof(*queue)) {
		return 0;
	}

	queue = (size_t *)malloc(count * sizeof(*queue));

	if (queue == NULL) {
		return 0;
	}

	tail = 0;

	for (x = 0; x < image->width; x++) {
		enqueue_transparent(image->rgba, queue, &tail, x);
		enqueue_transparent(
			image->rgba, queue, &tail, count - image->width + x);
	}

	for (y = 0; y < image->height; y++) {
		enqueue_transparent(
			image->rgba, queue, &tail, y * image->width);
		enqueue_transparent(
			image->rgba, queue, &tail, (y + 1) * image->width - 1);
	}

	head = 0;

	while (head < tail) {
		pixel = queue[head++];
		x = pixel % image->width;

		if (x != 0) {
			enqueue_transparent(
				image->rgba, queue, &tail, pixel - 1);
		}

		if (x + 1 < image->width) {
			enqueue_transparent(
				image->rgba, queue, &tail, pixel + 1);
		}

		if (pixel >= image->width) {
			enqueue_transparent(image->rgba, queue, &tail,
				pixel - image->width);
		}

		if (pixel < count - image->width) {
			enqueue_transparent(image->rgba, queue, &tail,
				pixel + image->width);
		}
	}

	free(queue);

	for (pixel = 0; pixel < count; pixel++) {
		if (image->rgba[pixel * 4 + 3] == LASSO_PENDING_ALPHA) {
			image->rgba[pixel * 4 + 3] = 255;
		}
	}

	return 1;
}

static enum mh_error_code
read_chunks(const unsigned char *data, size_t size, struct ilbm_chunks *output,
	struct mh_error *error_out)
{
	struct ilbm_chunks chunks;
	const unsigned char *chunk;
	unsigned long chunk_size;
	size_t form_end, cursor;

	if (data == NULL || size < FORM_HEADER_BYTES ||
		memcmp(data, "FORM", 4) != 0 ||
		memcmp(data + 8, "ILBM", 4) != 0) {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"missing or unsupported ILBM header");
	}

	chunk_size = read_u32be(data + 4);

	if (chunk_size < 4 || chunk_size > size - CHUNK_HEADER_BYTES) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ILBM FORM extends outside the file");
	}

	form_end = (size_t)chunk_size + CHUNK_HEADER_BYTES;
	chunks.bitmap_header = NULL;
	chunks.palette = NULL;
	chunks.body = NULL;
	chunks.body_size = 0;
	chunks.palette_count = 0;
	cursor = FORM_HEADER_BYTES;

	while (cursor < form_end) {
		if (form_end - cursor < CHUNK_HEADER_BYTES) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ILBM chunk header is truncated");
		}

		chunk = data + cursor;
		chunk_size = read_u32be(chunk + 4);
		cursor += CHUNK_HEADER_BYTES;

		if (chunk_size > form_end - cursor) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"ILBM chunk extends outside FORM");
		}

		/*
		 * Five shipped images exclude the final BODY pad from FORM
		 * size, but retain that byte in the file. Allow exactly this
		 * boundary quirk; the payload itself must remain entirely
		 * inside FORM.
		 */
		if ((chunk_size & 1UL) != 0 &&
			chunk_size == form_end - cursor &&
			(memcmp(chunk, "BODY", 4) != 0 || form_end == size)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ILBM final chunk is missing its pad byte");
		}

		if (memcmp(chunk, "BMHD", 4) == 0) {
			if (chunks.bitmap_header != NULL ||
				chunk_size != BITMAP_HEADER_BYTES) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate ILBM bitmap header");
			}

			chunks.bitmap_header = data + cursor;
		} else if (memcmp(chunk, "CMAP", 4) == 0) {
			if (chunks.palette != NULL || chunk_size == 0 ||
				chunk_size % 3 != 0 ||
				chunk_size > PALETTE_BYTES) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate ILBM palette");
			}

			chunks.palette = data + cursor;
			chunks.palette_count = (size_t)chunk_size / 3;
		} else if (memcmp(chunk, "BODY", 4) == 0) {
			if (chunks.body != NULL ||
				chunks.bitmap_header == NULL ||
				chunks.palette == NULL) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ILBM BODY is duplicated or precedes its properties");
			}

			chunks.body = data + cursor;
			chunks.body_size = (size_t)chunk_size;
		} else if (memcmp(chunk, "CAMG", 4) == 0) {
			if (chunk_size < 4 ||
				(read_u32be(data + cursor) &
					UNSUPPORTED_DISPLAY_MODES) != 0) {
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"ILBM HAM/EHB modes are not supported");
			}
		} else if (memcmp(chunk, "DEST", 4) == 0) {
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"ILBM destination-plane merging is not supported");
		}

		cursor += (size_t)chunk_size;

		if ((chunk_size & 1UL) != 0 && cursor < form_end) {
			cursor++;
		}
	}

	if (chunks.bitmap_header == NULL || chunks.palette == NULL ||
		chunks.body == NULL) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ILBM is missing bitmap, palette or pixel data");
	}

	*output = chunks;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_ilbm(const unsigned char *data, size_t size,
	const unsigned char *shared_palette, unsigned int shared_colors,
	int texture_alpha, struct mh_image *image,
	struct mh_image_palette *output_palette,
	struct mh_indexed_image *indexed, struct mh_error *error_out)
{
	struct mh_image decoded;
	struct mh_indexed_image indexed_image;
	struct ilbm_chunks chunks = {0};
	enum mh_error_code status;
	const unsigned char *palette;
	unsigned char *rows;
	unsigned char *pixel;
	size_t cursor;
	size_t palette_count;
	size_t row_bytes;
	size_t pixels, pixel_index;
	unsigned int planes;
	unsigned int stored_planes;
	unsigned int masking;
	unsigned int transparent_index;
	unsigned int index;
	unsigned int plane;
	unsigned int x;
	unsigned int y;

	if ((shared_palette == NULL && shared_colors != 0) ||
		(shared_palette != NULL &&
			(shared_colors == 0 || shared_colors > PALETTE_COLORS)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shared ILBM palette");

	if (image == NULL && indexed == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"missing or unsupported ILBM header");

	status = read_chunks(data, size, &chunks, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	palette = chunks.palette;
	palette_count = chunks.palette_count;

	if (shared_palette != NULL) {
		palette = shared_palette;
		palette_count = shared_colors;
	}

	decoded.width =
		(unsigned int)read_u16be(chunks.bitmap_header + BITMAP_WIDTH);
	decoded.height =
		(unsigned int)read_u16be(chunks.bitmap_header + BITMAP_HEIGHT);
	planes = chunks.bitmap_header[BITMAP_PLANES];
	masking = chunks.bitmap_header[BITMAP_MASK];
	transparent_index = (unsigned int)read_u16be(
		chunks.bitmap_header + BITMAP_TRANSPARENT_INDEX);

	if (decoded.width == 0 || decoded.height == 0 || planes == 0 ||
		planes > 8 || masking > MASK_LASSO ||
		chunks.bitmap_header[BITMAP_COMPRESSION] > 1) {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported ILBM dimensions, planes, mask or compression");
	}

	if (decoded.height > (size_t)-1 / decoded.width) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ILBM pixel count overflows");
	}

	pixels = (size_t)decoded.width * decoded.height;

	if (pixels > PIXEL_LIMIT || pixels > (size_t)-1 / 4) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ILBM exceeds decoded image size limit");
	}

	row_bytes = (((size_t)decoded.width + 15) / 16) * 2;
	stored_planes = planes + (masking == MASK_PLANE);

	if (row_bytes > (size_t)-1 / stored_planes) {
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "ILBM row size overflows");
	}

	rows = (unsigned char *)malloc(row_bytes * stored_planes);
	memset(&indexed_image, 0, sizeof(indexed_image));
	decoded.rgba =
		image == NULL ? NULL : (unsigned char *)malloc(pixels * 4);

	if (indexed != NULL)
		indexed_image.indices = (unsigned char *)malloc(pixels);

	if (rows == NULL || (image != NULL && decoded.rgba == NULL) ||
		(indexed != NULL && indexed_image.indices == NULL)) {
		free(rows);
		free(decoded.rgba);
		free(indexed_image.indices);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding ILBM");
	}

	cursor = 0;

	for (y = 0; y < decoded.height; y++) {
		for (plane = 0; plane < stored_planes; plane++) {
			if (!unpack_row(chunks.body, chunks.body_size, &cursor,
				rows + plane * row_bytes, row_bytes,
				chunks.bitmap_header[BITMAP_COMPRESSION])) {
				free(rows);
				free(decoded.rgba);
				free(indexed_image.indices);
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"ILBM row is truncated or its run exceeds the row");
			}
		}

		for (x = 0; x < decoded.width; x++) {
			index = 0;

			for (plane = 0; plane < planes; plane++) {
				if (rows[plane * row_bytes + x / 8] &
					(128U >> (x % 8))) {
					index |= 1U << plane;
				}
			}

			if (index >= palette_count) {
				free(rows);
				free(decoded.rgba);
				free(indexed_image.indices);
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ILBM pixel references an absent palette entry");
			}

			pixel_index = (size_t)y * decoded.width + x;

			if (indexed != NULL)
				indexed_image.indices[pixel_index] =
					(unsigned char)index;

			if (image == NULL)
				continue;

			pixel = decoded.rgba +
				((size_t)y * decoded.width + x) * 4;
			memcpy(pixel, palette + index * 3, 3);
			pixel[3] = 255;

			if (texture_alpha) {
				pixel[3] = index == 0 ? 0 : 255;
			} else if ((masking == MASK_COLOR &&
					index == transparent_index) ||
				(masking == MASK_PLANE &&
					!(rows[planes * row_bytes + x / 8] &
						(128U >> (x % 8))))) {
				pixel[3] = 0;
			} else if (masking == MASK_LASSO &&
				index == transparent_index) {
				pixel[3] = LASSO_PENDING_ALPHA;
			}
		}
	}

	free(rows);

	if (image != NULL && !texture_alpha && masking == MASK_LASSO &&
		!apply_lasso_mask(&decoded)) {
		free(decoded.rgba);
		free(indexed_image.indices);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding ILBM lasso mask");
	}

	if (output_palette != NULL) {
		memset(output_palette, 0, sizeof(*output_palette));
		memcpy(output_palette->rgb, palette, palette_count * 3);
		output_palette->count = (unsigned int)palette_count;
	}

	if (indexed != NULL) {
		indexed_image.width = decoded.width;
		indexed_image.height = decoded.height;
		indexed_image.size = pixels;
		indexed_image.colors = (unsigned int)palette_count;
		memcpy(indexed_image.palette, palette, palette_count * 3);
		*indexed = indexed_image;
	}

	if (image != NULL)
		*image = decoded;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_ilbm_decode(const unsigned char *data, size_t size,
	struct mh_image *image, struct mh_error *error_out)
{
	return mh_image_ilbm_decode_palette(
		data, size, NULL, 0, image, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_texture(const unsigned char *data, size_t size,
	struct mh_image *image, struct mh_error *error_out)
{
	return decode_ilbm(
		data, size, NULL, 0, 1, image, NULL, NULL, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_image_palette(const unsigned char *data, size_t size,
	struct mh_image *image, struct mh_image_palette *palette,
	struct mh_error *error_out)
{
	if (palette == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing image palette output");

	return decode_ilbm(
		data, size, NULL, 0, 0, image, palette, NULL, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_texture_palette(const unsigned char *data, size_t size,
	struct mh_image *image, struct mh_image_palette *palette,
	struct mh_error *error_out)
{
	if (palette == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing texture palette output");

	return decode_ilbm(
		data, size, NULL, 0, 1, image, palette, NULL, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_palette(const unsigned char *data, size_t size,
	const unsigned char *shared_palette, unsigned int shared_colors,
	struct mh_image *image, struct mh_error *error_out)
{
	return decode_ilbm(data, size, shared_palette, shared_colors, 0, image,
		NULL, NULL, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_indexed(const unsigned char *data, size_t size,
	struct mh_indexed_image *image, struct mh_error *error_out)
{
	if (image == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing indexed image output");

	return decode_ilbm(
		data, size, NULL, 0, 0, NULL, NULL, image, error_out);
}

enum mh_error_code
mh_image_ilbm_decode_rgba(const unsigned char *data, size_t size,
	const struct mh_image_palette *palette, int index_zero,
	struct mh_image *output, struct mh_image_palette *palette_output,
	struct mh_error *error_out)
{
	if (index_zero != 0 && index_zero != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed image alpha policy");

	return decode_ilbm(data, size, palette == NULL ? NULL : palette->rgb,
		palette == NULL ? 0 : palette->count, index_zero, output,
		palette_output, NULL, error_out);
}

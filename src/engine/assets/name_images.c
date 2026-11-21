#include <stdlib.h>
#include <string.h>

#include "engine/assets/name_images.h"

enum {
	FONT_GRID = 16,
	FONT_CELL_LIMIT = 64,
	IMAGE_WIDTH_LIMIT = 256,
	NAME_PADDING = 2
};

static enum mh_error_code
image_width(const struct mh_font *font, const struct mh_profile *profile,
	unsigned int *width, struct mh_error *error_out)
{
	unsigned int i, code, value;

	if (memchr(profile->nick, 0, sizeof(profile->nick)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated driver nickname");

	value = NAME_PADDING;

	for (i = 0; profile->nick[i] != 0; i++) {
		code = (unsigned char)profile->nick[i];

		if (font->advance[code] > IMAGE_WIDTH_LIMIT - value)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"nickname exceeds original image width");

		value += font->advance[code];
	}

	*width = value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
draw_name(const struct mh_font *font, const char *name,
	struct mh_indexed_image *image)
{
	unsigned int i, code, pen, x, y, glyph_x, glyph_y;
	size_t source, destination;

	pen = 1;

	for (i = 0; name[i] != 0; i++) {
		code = (unsigned char)name[i];
		glyph_x = (code % FONT_GRID) * font->cell_width;
		glyph_y = (code / FONT_GRID) * font->height;

		for (y = 0; y < font->height; y++)
			for (x = 0; x < font->advance[code]; x++) {
				source = ((size_t)(glyph_y + y) *
							font->image.width +
						glyph_x + x) *
					4;
				destination = (size_t)(y + 1) * image->width +
					pen + x;

				if (font->image.rgba[source + 3] != 0)
					image->indices[destination] = 1;
			}

		pen += font->advance[code];
	}
}

void
mh_name_images_free(struct mh_name_images *names)
{
	unsigned int i;

	if (names == NULL)
		return;

	for (i = 0; i < MH_NAME_LABELS_MAX; i++)
		free(names->images[i].indices);

	memset(names, 0, sizeof(*names));
}

enum mh_error_code
mh_name_images_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_images *output,
	struct mh_error *error_out)
{
	struct mh_name_images next;
	struct mh_indexed_image *image;
	unsigned int widths[MH_NAME_LABELS_MAX], i;
	unsigned long color;
	enum mh_error_code status;

	if (font == NULL || profiles == NULL || output == NULL || count < 1 ||
		count > MH_NAME_LABELS_MAX || active_mask >= (1U << count) ||
		font->image.rgba == NULL || font->cell_width < 1 ||
		font->cell_width > FONT_CELL_LIMIT || font->height < 2 ||
		font->height > FONT_CELL_LIMIT ||
		font->image.width != FONT_GRID * font->cell_width ||
		font->image.height != FONT_GRID * font->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driver name image inputs");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (font->advance[i] > font->cell_width)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid driver name font advance");

	for (i = 0; i < count; i++) {
		if (!(active_mask & (1U << i)))
			continue;

		status = image_width(font, &profiles[i], &widths[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	memset(&next, 0, sizeof(next));
	next.count = count;
	next.active_mask = active_mask;

	for (i = 0; i < count; i++) {
		if (!(active_mask & (1U << i)))
			continue;

		image = &next.images[i];
		image->width = widths[i];
		image->height = font->height + 1;
		image->colors = 2;
		image->size = (size_t)image->width * image->height;
		image->indices = calloc(image->size, 1);

		if (image->indices == NULL) {
			mh_name_images_free(&next);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory creating driver name images");
		}

		color = profiles[i].colors[0];
		image->palette[3] = (unsigned char)((color >> 16) & 255);
		image->palette[4] = (unsigned char)((color >> 8) & 255);
		image->palette[5] = (unsigned char)(color & 255);
		draw_name(font, profiles[i].nick, image);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

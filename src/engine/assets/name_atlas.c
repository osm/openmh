#include <stdlib.h>
#include <string.h>

#include "engine/assets/name_atlas.h"
#include "engine/core/error.h"

#define ATLAS_SIZE 256U
#define LABEL_SIZE .8f

enum {
	FONT_CELL_LIMIT = 64,
	FONT_GRID_SIZE = 16,
	NAME_PADDING = 2,
	FIRST_LABEL_ROW = 128,
	ROW_HEIGHT_LIMIT = 16,
	LABEL_HEIGHT = 2
};

/* The caller has validated nickname termination, advances and atlas bounds. */
static void
draw_name(const struct mh_font *font, const char *name, unsigned int row_y,
	unsigned int row_height, struct mh_image *image)
{
	unsigned int i, code, x, y, pen, glyph_x, glyph_y;
	size_t source, destination;

	pen = 1;

	for (i = 0; name[i] != 0; i++) {
		code = (unsigned char)name[i];
		glyph_x = (code % FONT_GRID_SIZE) * font->cell_width;
		glyph_y = (code / FONT_GRID_SIZE) * font->height;

		for (y = 0; y < row_height - 1; y++)
			for (x = 0; x < font->advance[code]; x++) {
				source = ((size_t)(glyph_y + y) *
							font->image.width +
						glyph_x + x) *
					4;
				destination =
					((size_t)(row_y + 1 + y) * ATLAS_SIZE +
						pen + x) *
					4;

				if (font->image.rgba[source + 3] != 0)
					memset(image->rgba + destination, 255,
						3);
			}

		pen += font->advance[code];
	}
}

void
mh_name_atlas_free(struct mh_name_atlas *atlas)
{
	if (atlas == NULL)
		return;

	free(atlas->image.rgba);
	memset(atlas, 0, sizeof(*atlas));
}

enum mh_error_code
mh_name_atlas_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_atlas *output,
	struct mh_error *error_out)
{
	struct mh_name_atlas result;
	unsigned int widths[MH_NAME_LABELS_MAX], row_y, row_height, entry, i,
		code;
	unsigned long color;
	size_t destination;

	if (font == NULL || profiles == NULL || output == NULL || count < 1 ||
		count > MH_NAME_LABELS_MAX || active_mask >= (1U << count) ||
		font->image.rgba == NULL || font->cell_width < 1 ||
		font->cell_width > FONT_CELL_LIMIT || font->height < 2 ||
		font->height > FONT_CELL_LIMIT ||
		font->image.width != FONT_GRID_SIZE * font->cell_width ||
		font->image.height != FONT_GRID_SIZE * font->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid name atlas inputs");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (font->advance[i] > font->cell_width)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid name font advance");

	for (entry = 0; entry < count; entry++) {
		widths[entry] = NAME_PADDING;

		if (!(active_mask & (1U << entry)))
			continue;

		if (memchr(profiles[entry].nick, 0,
			sizeof(profiles[entry].nick)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated name atlas nickname");

		for (i = 0; profiles[entry].nick[i] != 0; i++) {
			code = (unsigned char)profiles[entry].nick[i];

			if (font->advance[code] > ATLAS_SIZE - widths[entry])
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"nickname exceeds original atlas width");

			widths[entry] += font->advance[code];
		}
	}

	memset(&result, 0, sizeof(result));
	result.count = count;
	result.active_mask = active_mask;
	result.image.width = result.image.height = ATLAS_SIZE;
	result.image.rgba = (unsigned char *)calloc(ATLAS_SIZE * ATLAS_SIZE, 4);

	if (result.image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating name atlas");

	for (destination = 0; destination < ATLAS_SIZE * ATLAS_SIZE;
		destination++)
		result.image.rgba[destination * 4 + 3] = 255;

	row_height = font->height + 1;

	if (row_height > ROW_HEIGHT_LIMIT)
		row_height = ROW_HEIGHT_LIMIT;

	/* Larger rosters use the previously empty upper half of the atlas. */
	row_y = count > (ATLAS_SIZE - FIRST_LABEL_ROW) / row_height
		? 0
		: FIRST_LABEL_ROW;

	for (entry = 0; entry < count; entry++) {
		struct mh_name_label *label;

		if (!(active_mask & (1U << entry)))
			continue;

		label = &result.labels[entry];
		color = profiles[entry].colors[0];
		label->color[0] = (float)((color >> 16) & 255);
		label->color[1] = (float)((color >> 8) & 255);
		label->color[2] = (float)(color & 255);
		label->height = LABEL_HEIGHT;
		label->size[0] = label->size[1] = LABEL_SIZE;
		label->uv[0] = 1 / (float)ATLAS_SIZE;
		label->uv[1] = (row_y + 1) / (float)ATLAS_SIZE;
		label->uv[2] = (widths[entry] - 1) / (float)ATLAS_SIZE;
		label->uv[3] = (row_y + row_height - 1) / (float)ATLAS_SIZE;
		draw_name(font, profiles[entry].nick, row_y, row_height,
			&result.image);

		row_y += row_height;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

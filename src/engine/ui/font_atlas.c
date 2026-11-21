#include "engine/ui/font_atlas.h"

#define FONT_COLUMNS 16

enum mh_error_code
mh_ui_font_atlas_add(struct mh_ui_atlas *atlas, const struct mh_font *font,
	unsigned int right_trim, struct mh_ui_region glyphs[MH_FONT_GLYPHS],
	unsigned int advances[MH_FONT_GLYPHS], struct mh_error *error_out)
{
	struct mh_ui_region region;
	enum mh_error_code status;
	unsigned int i;

	if (font == NULL || glyphs == NULL || advances == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UI atlas font");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (font->advance[i] < right_trim)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid UI atlas glyph trim");

	status = mh_ui_atlas_add(atlas, &font->image, &region, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		advances[i] = font->advance[i];
		glyphs[i].x = region.x + (i % FONT_COLUMNS) * font->cell_width;
		glyphs[i].y = region.y + (i / FONT_COLUMNS) * font->height;
		glyphs[i].width = font->advance[i] - right_trim;
		glyphs[i].height = font->height;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

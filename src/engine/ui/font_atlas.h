#ifndef MH_UI_FONT_ATLAS_H
#define MH_UI_FONT_ATLAS_H

#include "engine/ui/atlas.h"
#include "interfaces/font/glyphs.h"

/*
 * Pack a decoded 16-by-16 font and publish its glyph regions/advances on
 * success. Borrow font pixels; the caller frees them on success or failure.
 * Right trim excludes trailing spacing from glyph rectangles without changing
 * text advances: hardware fonts use one, software-derived fonts use zero.
 * Failure preserves the atlas and both output arrays.
 */
enum mh_error_code	mh_ui_font_atlas_add(struct mh_ui_atlas *atlas,
	const struct mh_font *font, unsigned int right_trim,
	struct mh_ui_region glyphs[MH_FONT_GLYPHS],
	unsigned int advances[MH_FONT_GLYPHS], struct mh_error *error_out);

#endif

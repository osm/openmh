#ifndef MH_SOFTWARE_TEXT_H
#define MH_SOFTWARE_TEXT_H

#include "backends/render/software_raster.h"
#include "engine/ui/draw.h"

/*
 * Borrow a decoded immutable FNT byte-write map. Text contains length nonzero
 * character bytes, at most 1024; empty text is valid. Width floors the total
 * byte advance, not each glyph's advance, in word mode.
 */
enum mh_error_code	mh_software_text_measure(
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, unsigned int *width, struct mh_error *error_out);

/* Check a complete command before any other overlay drawing takes place. */
enum mh_error_code	mh_software_text_check(
	const struct mh_software_surface *surface,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int color,
	enum mh_ui_text_align align, struct mh_error *error_out);

/*
 * Word foreground writes, including unaligned glyphs. Color is a packed 16-bit
 * value. Bounds clipping protects the surface. Validate before writing; no
 * allocations. Coordinates are bounded to +/-16384.
 */
enum mh_error_code	mh_software_text_draw(
	const struct mh_software_surface *surface,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int color,
	enum mh_ui_text_align align, struct mh_error *error_out);

#endif

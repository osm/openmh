#ifndef MH_ASSETS_DEMO_FONT_H
#define MH_ASSETS_DEMO_FONT_H

#include "engine/core/error.h"
#include "engine/core/file.h"

/*
 * Convert a word-pixel FNT to byte-pixel spans, preserving glyph coverage,
 * scanlines and logical advances. Output owns its bytes; unchanged on failure.
 */
enum mh_error_code	mh_font_fnt_byte_pixels(const struct mh_file *source,
	struct mh_file *output, struct mh_error *error_out);

#endif

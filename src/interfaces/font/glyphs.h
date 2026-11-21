#ifndef MH_FONT_GLYPHS_H
#define MH_FONT_GLYPHS_H

#include "interfaces/image/pixels.h"

#define MH_FONT_GLYPHS 256

enum mh_font_byte {
	MH_FONT_SKIP,
	MH_FONT_LOW,
	MH_FONT_HIGH,
	MH_FONT_WIDE_LOW
};

/*
 * Decoded byte-write map in 16 by 16 glyph cells. Each byte identifies
 * the source foreground byte, independently of its destination alignment.
 * WIDE_LOW preserves quieting of the high foreground byte when required by
 * the original software text behavior. This is a decoded operation map,
 * never executable instructions or offsets into a file.
 * Advances and cell_width are bytes; height is scanlines, rows atlas rows.
 */
struct mh_font_program {
	unsigned char	*bytes;
	unsigned int	width, rows, cell_width, height;
	unsigned int	advance[MH_FONT_GLYPHS];
};

struct mh_font {
	/* 16 by 16 glyph cells; white with binary alpha. */
	struct mh_image	image;
	unsigned int	advance[MH_FONT_GLYPHS];
	unsigned int	cell_width, height;
};

#endif

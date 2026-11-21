#ifndef MH_SPRITE_PIXELS_H
#define MH_SPRITE_PIXELS_H

#include "interfaces/image/pixels.h"

/* Decoded top-down frames; successful readers transfer malloc-owned pixels. */
struct mh_sprite_indexed {
	unsigned int	width, height;

	/* Interleaved palette index, coverage (0 or 255). */
	unsigned char	*pixels;
};

struct mh_sprite_words {
	unsigned int	width, height;

	/* Little-endian color word, coverage: three bytes. */
	unsigned char	*pixels;
};

#endif

#ifndef MH_INTERFACES_IMAGE_PIXELS_H
#define MH_INTERFACES_IMAGE_PIXELS_H

#include <stddef.h>

#define MH_IMAGE_PALETTE_COLORS 256
#define MH_IMAGE_PALETTE_BYTES (MH_IMAGE_PALETTE_COLORS * 3)

/*
 * Tightly packed RGBA8 pixels, with rows stored top to bottom.
 * Allocation ownership is specified by the operation returning the image.
 */
struct mh_image {
	unsigned int	width;
	unsigned int	height;
	unsigned char	*rgba;
};

/* Owned, tightly packed top-to-bottom indices and an unmodified RGB palette. */
struct mh_indexed_image {
	unsigned int	width, height, colors;
	size_t		size;
	unsigned char	*indices;

	/* 256 RGB entries. */
	unsigned char	palette[MH_IMAGE_PALETTE_BYTES];
};

/* Owned RGB palette copy; count zero denotes a true-color image. */
struct mh_image_palette {
	unsigned char	rgb[MH_IMAGE_PALETTE_BYTES];
	unsigned int	count;
};

/* Borrowed top-down pixels. Packed 16-bit words are little endian. */
enum mh_picture_format {
	MH_PICTURE_INDEXED = 1,
	MH_PICTURE_RGB565 = 3,
	MH_PICTURE_RGB555 = 4,
	MH_PICTURE_RGBA8 = 5
};

struct mh_picture {
	enum mh_picture_format	format;
	unsigned int		width, height, palette_count;
	size_t			pitch, pixel_size, palette_size;
	const unsigned char *pixels,
		*palette; /* Top-down rows; palette is RGB. */
};

#endif

#ifndef MH_SOFTWARE_SKY_IMAGE_H
#define MH_SOFTWARE_SKY_IMAGE_H

#include <stddef.h>

#include "engine/core/error.h"

/* Prepared, owned little-endian RGB555/565 pixels, top row first. */
struct mh_software_sky_image {
	unsigned int	width, height;
	size_t		size;
	unsigned char	*pixels;
};

/* Original loader dimensions. Errors preserve both outputs. */
enum mh_error_code	mh_software_sky_size(unsigned int source_width,
	unsigned int source_height, unsigned int view_width,
	unsigned int view_height, unsigned int *width, unsigned int *height,
	struct mh_error *error_out);

/*
 * Filter tightly packed top-down BGR24 into packed RGB555/565. green_bits is 5
 * or 6. Caller supplies the desired output dimensions. On success caller owns
 * output->pixels and frees it with free(). Errors preserve output; release an
 * earlier image before replacing its owner.
 */
enum mh_error_code	mh_software_sky_resize(const unsigned char *bgr,
	size_t size, unsigned int source_width, unsigned int source_height,
	unsigned int width, unsigned int height, unsigned int green_bits,
	struct mh_software_sky_image *output, struct mh_error *error_out);

#endif

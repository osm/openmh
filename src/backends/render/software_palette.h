#ifndef MH_SOFTWARE_PALETTE_H
#define MH_SOFTWARE_PALETTE_H

#include <stddef.h>

#include "backends/render/software_raster.h"

#define MH_SOFTWARE_PALETTE_COLORS 256

struct mh_software_palette {
	unsigned int	color[MH_SOFTWARE_PALETTE_COLORS];
	unsigned int	count, green_bits;
};

/*
 * Original per-triangle palette lighting/fog; green_bits selects 555 or 565.
 * Errors preserve output. Success clears unused entries beyond count.
 */
enum mh_error_code	mh_software_palette_build(
	struct mh_software_palette *output, const unsigned char *rgb,
	size_t size, unsigned int count,
	const struct mh_software_triangle *triangle, unsigned int green_bits,
	struct mh_error *error_out);

#endif

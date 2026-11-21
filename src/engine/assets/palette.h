#ifndef MH_ASSETS_PALETTE_H
#define MH_ASSETS_PALETTE_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/image/palette.h"

/*
 * Default.ACT: exactly 256 RGB triples. Output is 0xRRGGBB in file order,
 * unchanged on error. The original car-color list consumes 255 down to 224.
 */
enum mh_error_code	mh_palette_act_decode(const unsigned char *data,
	size_t size, unsigned long output[MH_PALETTE_COLORS],
	struct mh_error *error_out);

#endif

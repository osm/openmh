#ifndef MH_ASSETS_PORTRAIT_H
#define MH_ASSETS_PORTRAIT_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/image/portrait.h"

/*
 * Results portraits: fixed 18-byte header and grayscale byte payload, other TGA
 * metadata ignored, vertical origin bit only, 32-level ramp, or opaque black
 * when the resource could not be loaded. Pass NULL/0 for that fallback. Length
 * checks reject truncated resources. Output owns its RGBA allocation; errors
 * preserve output.
 */
enum mh_error_code	mh_portrait_tga_decode(const unsigned char *data,
	size_t size, struct mh_image *output, struct mh_error *error_out);

#endif

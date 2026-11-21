#ifndef MH_ASSETS_ICO_H
#define MH_ASSETS_ICO_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

/*
 * Original Windows icons: uncompressed 4/8-bit indexed DIB with AND mask.
 * ICO selects its first image; DIB accepts a standalone RT_ICON resource.
 * Output is owned, top-down RGBA. Caller frees rgba; failure preserves output.
 */
enum mh_error_code	mh_image_ico_decode(const unsigned char *data,
	size_t size, struct mh_image *output, struct mh_error *error_out);
enum mh_error_code	mh_image_ico_decode_dib(const unsigned char *data,
	size_t size, struct mh_image *output, struct mh_error *error_out);

#endif

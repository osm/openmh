#ifndef MH_LOADING_RESIZE_H
#define MH_LOADING_RESIZE_H

#include "engine/assets/image.h"

/*
 * Original software loading bitmap resize: RGB565, endpoint-aligned 16.16
 * steps, four-bit weights, vertical then horizontal truncation. Coverage uses
 * the separate centered nearest sample. Borrows source RGBA8 pixels during
 * the call; output owns its new RGBA8 allocation and is unchanged on failure.
 * No GPU filtering policy is changed.
 */
enum mh_error_code	mh_loading_image_resize(const struct mh_image *source,
	unsigned int width, unsigned int height, int masked,
	struct mh_image *output, struct mh_error *error_out);

#endif

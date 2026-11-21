#ifndef MH_SOFTWARE_BLUR_H
#define MH_SOFTWARE_BLUR_H

#include "backends/render/software_raster.h"

/*
 * Blend the current scene with borrowed history in place, before drawing UI.
 * Average packed channels with truncation, row by row. Complete visible pixel
 * pairs are blended; padding and an odd final pixel remain untouched. Borrowed
 * history uses the same pitch as the scene. The caller retains the result as
 * the next history frame.
 */
enum mh_error_code	mh_software_blur(struct mh_software_surface *surface,
	const unsigned char *history, size_t size, struct mh_error *error_out);

#endif

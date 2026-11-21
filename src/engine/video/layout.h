#ifndef MH_VIDEO_LAYOUT_H
#define MH_VIDEO_LAYOUT_H

#include "engine/core/error.h"

/*
 * Center video frames. Guards reject oversized/empty frames instead of wrapping
 * unsigned subtraction.
 */
enum mh_error_code	mh_video_origin(unsigned int frame_width,
	unsigned int frame_height, unsigned int buffer_width,
	unsigned int buffer_height, unsigned int *x, unsigned int *y,
	struct mh_error *error_out);

#endif

#include <stddef.h>

#include "engine/video/layout.h"

enum mh_error_code
mh_video_origin(unsigned int frame_width, unsigned int frame_height,
	unsigned int buffer_width, unsigned int buffer_height, unsigned int *x,
	unsigned int *y, struct mh_error *error_out)
{
	if (x == NULL || y == NULL || frame_width == 0 || frame_height == 0 ||
		frame_width > buffer_width || frame_height > buffer_height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid movie frame dimensions");

	*x = (buffer_width - frame_width) / 2;
	*y = (buffer_height - frame_height) / 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

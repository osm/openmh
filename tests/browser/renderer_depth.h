#ifndef MH_WEBGL_RENDERER_DEPTH_CHECK_H
#define MH_WEBGL_RENDERER_DEPTH_CHECK_H

#include <stddef.h>

struct mh_renderer;

int	mh_webgl_test_read_depth(struct mh_renderer *renderer,
	unsigned char *visible, size_t count);

#endif

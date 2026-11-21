#ifndef MH_INTERFACES_RENDER_NAMES_H
#define MH_INTERFACES_RENDER_NAMES_H

#include <stddef.h>

struct mh_indexed_image;

/*
 * Software driver labels, borrowed for one lens frame.
 * Insert before the body's scene part, ahead of its halos and faces. Sources
 * are in part order.
 * model is the displayed body's affine transform; the image retains its own
 * palette. Hidden body parts suppress their names as well.
 */
struct mh_renderer_name {
	size_t				part;
	const float			*model;
	const struct mh_indexed_image	*image;
	float				height, size[2];
};

struct mh_renderer_names {
	const struct mh_renderer_name	*sources;
	size_t				count;
	int				transparent;
};

#endif

#ifndef MH_INTERFACES_RENDER_GLENZ_H
#define MH_INTERFACES_RENDER_GLENZ_H

#include <stddef.h>

/* Separate coordinates for the two PC 3.1 car-material passes. */
struct mh_glenz_coordinates {
	float	reflection[2];
	float	phong[2];
};

/*
 * Coordinates cover the complete mesh and are copied during submission.
 * Images are indices in that mesh's immutable texture table. Applies only
 * to eligible environment faces; ordinary materials remain unchanged.
 */
struct mh_renderer_glenz {
	const struct mh_glenz_coordinates	*coordinates;
	size_t					count;
	size_t					reflection_texture;
	size_t					phong_texture;
};

#endif

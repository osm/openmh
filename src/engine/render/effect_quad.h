#ifndef MH_EFFECT_QUAD_H
#define MH_EFFECT_QUAD_H

/* Screen-space original hardware effects, independent of the GPU API. */
struct mh_effect_quad {
	float	xy[4][2], uv[4][2], color[3], depth;
	float	reciprocal_w; /* Retained halo depth for PowerSGL submission. */
};

#endif

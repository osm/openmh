#ifndef MH_SOFTWARE_NEAR_CLIP_H
#define MH_SOFTWARE_NEAR_CLIP_H

#include "engine/render/near_clip.h"

struct mh_software_clipped_corner {
	float	xy[2], weight, uv[2], rgb[3];
};

struct mh_software_clipped_face {
	unsigned int				flags, count;
	struct mh_software_clipped_corner	corners[4];
};

/*
 * Clip in scene binary32 precision. Inputs use positive-forward camera
 * coordinates and mh_near_flags. near_mask marks behind-near corners (including
 * equality in the point projector). Quads split as 012 and 023; at most two
 * triangle/quad faces are returned in original list order. XY is projected and
 * weight is raw 1/Z, without Glide's depth bias. Common viewport outcodes cull
 * faces; this does not clip XY edges. inverse_near is not consumed: this path
 * computes 1/Z after intersection. Errors preserve outputs. Culling writes
 * count=0 without changing faces.
 */
enum mh_error_code	mh_software_clip_near(
	const struct mh_near_projection *projection, unsigned int flags,
	unsigned int near_mask, const struct mh_near_corner corners[4],
	struct mh_software_clipped_face output[2], unsigned int *count,
	struct mh_error *error_out);

#endif

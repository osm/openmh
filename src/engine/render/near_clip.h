#ifndef MH_NEAR_CLIP_H
#define MH_NEAR_CLIP_H

#include "engine/core/error.h"
#include "engine/render/clip_codes.h"

/* Descriptor bits retained by the original near-plane splitter. */
enum mh_near_flags {
	MH_NEAR_QUAD = 1,
	MH_NEAR_TEXTURED = 2,
	MH_NEAR_VERTEX_COLOR = 8,
	/* The splitter also accepts the alternate per-corner color bit 4. */
	MH_NEAR_COLOR_MASK = 12
};

/*
 * Shared by hardware scene submission. Camera coordinates have positive forward
 * Z. All factors are binary32 values from the original lens, not an SDL/OpenGL
 * projection matrix.
 */
struct mh_near_projection {
	float	near_distance, inverse_near, focal, half_width, aspect;
	float	center[2], extent[2];
};

struct mh_near_corner {
	float	camera[3], uv[2], rgb[3];
};

struct mh_projected_corner {
	float	xy[2], depth, reciprocal_w, uv[2], rgb[3];
};

struct mh_projected_face {
	unsigned int			flags, count;
	struct mh_projected_corner	corners[4];
};

/*
 * Use only for the original near-clipped branch. Flags retain descriptor
 * meanings: 1=quad, 2=textured, 4/8=per-corner color. Mask bits identify
 * behind-near corners (the upstream classifier remains separate).
 * A quad clips triangles 012 and 023 independently, then rejects each only
 * if all its projected corners share a viewport outcode. At most two faces
 * are published, in original linked-list order. Unused attributes are zero.
 * Finite attributes must be within +/-1e9, and visible Z at least 1e-9.
 * Errors preserve outputs. Culling writes count=0, leaving faces intact.
 * Input corners are not mutated and may alias output storage.
 */
enum mh_error_code	mh_clip_near(
	const struct mh_near_projection *projection, unsigned int flags,
	unsigned int near_mask, const struct mh_near_corner corners[4],
	struct mh_projected_face output[2], unsigned int *count,
	struct mh_error *error_out);

#endif

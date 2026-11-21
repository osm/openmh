#ifndef MH_SCENE_FACE_H
#define MH_SCENE_FACE_H

#include "engine/core/error.h"
#include "engine/render/projection.h"

enum mh_scene_pass {
	MH_SCENE_DISTANT_SOLID,
	MH_SCENE_DISTANT_ALPHA,
	MH_SCENE_SOLID,
	MH_SCENE_TEXTURED,
	MH_SCENE_ENVIRONMENT,
	MH_SCENE_ALPHA,
	MH_SCENE_UNAVAILABLE,
	MH_SCENE_PASSES
};

/*
 * Prepared scene attributes after projection and material selection. depth is
 * reversed [0,1]. RGB is in byte units; visibility/background are separately
 * quantized bytes. UV and reciprocal W are meaningful for textured, environment
 * and alpha faces. No raw file structure casts.
 */
struct mh_scene_vertex {
	float		xy[2], rgb[3], depth, reciprocal_w, uv[2];
	unsigned int	visibility, background;
};

struct mh_scene_corner {
	struct mh_projected_point	point;

	/* Already shaded RGB in byte units. */
	float				uv[2], rgb[3];
};

struct mh_scene_face {
	enum mh_scene_pass	pass;
	unsigned int		count, bucket;
	int			smooth;
	struct mh_scene_vertex	corners[4];
};

#endif

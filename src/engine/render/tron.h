#ifndef MH_TRON_H
#define MH_TRON_H

#include "engine/core/error.h"
#include "engine/render/scene_face.h"

/*
 * Screen-space outline attributes. RGB/alpha use 0..255 units; depth and
 * reciprocal W retain the caller's projection convention. Texture coordinates
 * are not consumed by this untextured outline pass.
 */
struct mh_tron_line_vertex {
	float	xy[2], rgb[3], depth, alpha, reciprocal_w;
};

enum mh_tron_blend {
	MH_TRON_REPLACE,
	MH_TRON_BACKGROUND,   /* src*(1-alpha) + dst*alpha */
	MH_TRON_SOURCE_ALPHA, /* src*alpha + dst*(1-alpha) */
	MH_TRON_ADD
};

struct mh_tron_style {
	enum mh_tron_blend	blend;
	int			fog, texture_alpha, vertex_alpha;

	/* Original reversed-Z depth units; zero omits outlines. */
	unsigned int		outline_bias;
};

struct mh_tron_vertex {
	struct mh_tron_line_vertex	line;

	/* UV * 256 * reciprocal W, with original float stores. */
	float				texture_w[2];
};

struct mh_tron_face {
	struct mh_tron_vertex	vertices[4];
	unsigned int		count;
	enum mh_scene_pass	pass;
};

/*
 * Original endpoint clipping order, including exclusive right/bottom tests.
 * No input mutation. Culled lines set visible=0 and preserve output; errors
 * preserve both outputs. Finite attributes within +/-1e9 and viewports
 * 1..16384 are accepted. Parallel outside lines are safely culled instead
 * of allowing the original division by zero and invalid interpolation.
 */
enum mh_error_code	mh_tron_clip_line(unsigned int width,
	unsigned int height, const struct mh_tron_line_vertex input[2],
	struct mh_tron_line_vertex output[2], int *visible,
	struct mh_error *error_out);

/*
 * All passes write depth with reversed GEQUAL. Texture RGB never contributes;
 * texture alpha is used only by the two alpha passes, with POINT sampling
 * and GREATER/zero rejection. NULL for an invalid pass.
 */
const struct mh_tron_style	*mh_tron_style(enum mh_scene_pass pass);

/*
 * Three/four corners, per-corner RGB unless textured/unavailable and smooth=0.
 * Output unchanged on invalid input. Fill triangles use 012 and (for quads)
 * 023; the outline helper emits only the perimeter, never diagonal 02.
 */
enum mh_error_code	mh_tron_prepare_face(enum mh_scene_pass pass,
	unsigned int count, int smooth, const struct mh_scene_vertex *corners,
	struct mh_tron_face *output, struct mh_error *error_out);

/*
 * Up to four clipped line pairs, in perimeter order. Depth bias is returned
 * by mh_tron_style and is not added to the original submitted coordinates.
 */
enum mh_error_code	mh_tron_face_outline(const struct mh_tron_face *face,
	unsigned int width, unsigned int height,
	struct mh_tron_line_vertex output[8], unsigned int *count,
	struct mh_error *error_out);

#endif

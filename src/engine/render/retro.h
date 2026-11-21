#ifndef MH_RETRO_H
#define MH_RETRO_H

#include "engine/core/error.h"
#include "interfaces/retro/data.h"

enum {
	MH_RETRO_VISIBLE_LINES = 4,
	MH_RETRO_QUAD_VERTICES = 6,
	MH_RETRO_BACKDROP_QUADS = 5,
	MH_RETRO_BACKDROP_VERTICES =
		MH_RETRO_BACKDROP_QUADS * MH_RETRO_QUAD_VERTICES
};

struct mh_retro_state {
	unsigned int	line;
	float		scroll, wave, time;
};

struct mh_retro_frame {
	struct mh_retro_state	next;
	unsigned int		lines[MH_RETRO_VISIBLE_LINES];
	float			scroll[MH_RETRO_VISIBLE_LINES], wave, center;
};

struct mh_retro_vertex {
	/* Pixels; original 16-bit depth units, RGBA bytes. */
	float	xy[2], depth, rgb[3], alpha;
};

struct mh_retro_polygon {
	unsigned int		count;
	struct mh_retro_vertex	vertices[4];
};

/*
 * Text/animation scheduling. State starts at zero. Samples four lines before
 * advancing; the last three blank slots are lookahead only. Error preserves
 * output; no text/resource ownership in this interface.
 */
enum mh_error_code	mh_retro_frame(float dt,
	const struct mh_retro_state *state, struct mh_retro_frame *output,
	struct mh_error *error_out);

/*
 * One of five original mesh instances, in source face order. Caller owns
 * capacity for MH_RETRO_MAX_FACES; visible faces alone are published. Input
 * data must satisfy the decoded retro source contract. Errors preserve outputs.
 */
enum mh_error_code	mh_retro_polygons(const struct mh_retro_assets *assets,
	unsigned int instance, unsigned int width, unsigned int height,
	float time, struct mh_retro_polygon output[MH_RETRO_MAX_FACES],
	unsigned int *count, struct mh_error *error_out);

/*
 * Glyph bend/brightness/depth. Input XY is the hardware font's aligned logical
 * quad, before this special projection. White text uses the returned RGB; UV
 * and alpha come from the ordinary font descriptor.
 */
enum mh_error_code	mh_retro_glyph(unsigned int width, unsigned int height,
	unsigned int font_height, float center, float scroll, float wave,
	const float xy[4][2], struct mh_retro_vertex output[4],
	struct mh_error *error_out);

/*
 * Five backdrop quads expanded in original triangle/corner order. These
 * write depth with ALWAYS before text/meshes use GEQUAL in reversed Z.
 */
enum mh_error_code	mh_retro_backdrop(unsigned int width,
	unsigned int height, float center,
	struct mh_retro_vertex output[MH_RETRO_BACKDROP_VERTICES],
	struct mh_error *error_out);

#endif

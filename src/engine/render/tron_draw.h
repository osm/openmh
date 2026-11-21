#ifndef MH_TRON_DRAW_H
#define MH_TRON_DRAW_H

#include "engine/render/tron.h"
#include "interfaces/render/renderer.h"

#define MH_TRON_MAX_FACES 1000000

enum {
	MH_TRON_FILL_VERTICES_MAX = 6,
	MH_TRON_OUTLINE_VERTICES_MAX = 8,
	MH_TRON_FACE_VERTICES_MAX =
		MH_TRON_FILL_VERTICES_MAX + MH_TRON_OUTLINE_VERTICES_MAX
};

struct mh_tron_draw {
	unsigned int	fill_first, fill_count, outline_first, outline_count;
	enum mh_scene_pass	pass;
};

/*
 * Expand one face into fill triangles and clipped perimeter lines. Returned
 * ranges are relative to vertices. No output changes on invalid input.
 * Outlines retain screen-linear attributes; they never consume reciprocal W.
 */
enum mh_error_code	mh_tron_draw_face(
	const struct mh_renderer_tron_face *input, unsigned int width,
	unsigned int height, struct mh_tron_draw *draw,
	struct mh_tron_vertex vertices[MH_TRON_FACE_VERTICES_MAX],
	struct mh_error *error_out);

#endif

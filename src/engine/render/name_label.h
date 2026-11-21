#ifndef MH_RENDER_NAME_LABEL_H
#define MH_RENDER_NAME_LABEL_H

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/render/effect_quad.h"
#include "engine/render/projection.h"

#define MH_NAME_LABELS_MAX MH_RACE_MAX_CARS

/*
 * Name label appearance. UVs address the 256px effects atlas. RGB is in [0,255]
 * units.
 */
struct mh_name_label {
	float	color[3], height, size[2], uv[4]; /* U0,V0,U1,V1. */
};

struct mh_name_quad {
	struct mh_effect_quad	quad;
	float			reciprocal_depth;
	int			visible;
};

/*
 * Parallel name/model lists. In-car omits the viewed slot; otherwise external
 * chase suppresses the local slot's name only. Availability does not suppress
 * names. Invalid roster inputs return zero.
 */
unsigned int	mh_name_label_mask(unsigned int count, unsigned int active_mask,
	unsigned int local, unsigned int viewed, int in_car, int chase);

/*
 * Hardware name labels. Positions are the displayed body's world and camera
 * translations; view is the positive-Z 12-float affine transform. Invisible
 * produces a zero result. Invalid input leaves output unchanged.
 */
enum mh_error_code	mh_name_label_project(const struct mh_name_label *label,
	const float world_position[3], const float camera_position[3],
	const float view[12], const struct mh_projection *projection,
	struct mh_name_quad *output, struct mh_error *error_out);

/* Width uses U1-U0; edge fades use reciprocal multiplies. */
enum mh_error_code	mh_patched_name_label_project(
	const struct mh_name_label *label, const float world_position[3],
	const float camera_position[3], const float view[12],
	const struct mh_projection *projection, struct mh_name_quad *output,
	struct mh_error *error_out);

#endif

#ifndef MH_HALO_INTERNAL_H
#define MH_HALO_INTERNAL_H

#include "engine/render/halo.h"

/*
 * Profile-owned depth, screen bounds and lens colors; no geometry ownership.
 * Reciprocal depth bypasses world-lamp visibility queries and supplies glow W.
 * Clip bounds are viewport fractions; biases adjust reciprocal W in view space.
 */
struct mh_halo_policy {
	int	reciprocal_depth;
	float	clip_min, clip_max;
	double	world_bias, attached_bias, directional_bias;
	double	lens_colors[MH_LENS_QUADS][3];
};

/*
 * Valid policies are supplied by profile entry points, never external inputs.
 */
enum mh_error_code	mh_halo_world_lamp_quads(
	const struct mh_halo_projection *projection, int previous_visible,
	int flares, struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	const struct mh_halo_policy *policy, struct mh_error *error_out);
enum mh_error_code	mh_halo_attached_quads(
	const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int previous_visible, int flares,
	struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	const struct mh_halo_policy *policy, struct mh_error *error_out);

#endif

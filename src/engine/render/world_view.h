#ifndef MH_WORLD_VIEW_H
#define MH_WORLD_VIEW_H

#include "engine/core/error.h"
#include "engine/render/frustum.h"

/*
 * Camera corners projected onto X/Z relative to the world's stored origin.
 * The selected rectangle is half-open, traversed Z first then X. Bounds
 * are the original expanded world-space min/max X then min/max Z.
 */
struct mh_world_view {
	struct mh_frustum	frustum;
	float			corners[8][2];
	float			bounds[4];
	unsigned long		first[2], end[2];
	float			camera[3];
	enum mh_render_profile	profile;
};

/*
 * Lens/pose constraints are shared with mh_frustum_setup. Spacing below one
 * selects no cells; invalid inputs or unsafe original int32 conversions
 * preserve output.
 */
enum mh_error_code	mh_world_view_setup(unsigned int width,
	unsigned int height, float focal, float near_distance,
	float far_distance, const float pose[12], const float origin[3],
	const float spacing[2], const unsigned long dimensions[2],
	struct mh_world_view *output, struct mh_error *error_out);

/* Software scene transforms/grid arithmetic round to binary32. */
enum mh_error_code	mh_world_view_setup_profile(
	enum mh_render_profile profile, unsigned int width, unsigned int height,
	float focal, float near_distance, float far_distance,
	const float pose[12], const float origin[3], const float spacing[2],
	const unsigned long dimensions[2], struct mh_world_view *output,
	struct mh_error *error_out);

#endif

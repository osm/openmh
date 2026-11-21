#ifndef MH_SOFTWARE_SKIDS_H
#define MH_SOFTWARE_SKIDS_H

#include "backends/render/software_skid_face.h"
#include "engine/game/race_limits.h"
#include "engine/render/world_view.h"
#include "engine/simulation/skids.h"
#include "interfaces/render/lighting.h"

#define MH_SOFTWARE_SKID_CARS MH_RACE_MAX_CARS

/* Reusable preparation scratch. Zero initialize before first use. */
struct mh_software_skids {
	struct mh_software_skid_face	*faces;
	size_t				count, capacity;
	struct mh_point_light		*lights;
	size_t				light_capacity;
	int				oldest_visible[MH_SOFTWARE_SKID_CARS][4];
};

struct mh_software_skid_view {
	const struct mh_projection	*projection;

	/* Positive-Z column-major affine view, 12 floats. */
	const float			*camera;
	const struct mh_world_view	*world_view;
	const struct mh_depth_cue	*cue;

	/* Parsed immutable world, or NULL to use lighting's explicit lights. */
	const struct mh_world		*world;
	const struct mh_model_lighting	*lighting;
};

/*
 * Append accepted quads in car/wheel/segment/subdivision order. Flat colors
 * remain signed and unclamped. History inputs are borrowed and never changed.
 * Preparation publishes scratch oldest_visible only on success. The caller
 * publishes frame feedback after drawing succeeds. Failure invalidates scratch
 * faces (count=0), preserves feedback and retains owned allocations for reuse.
 * Existing capacity avoids frame-time allocation.
 */
enum mh_error_code	mh_software_skids_prepare(
	struct mh_software_skids *state, const struct mh_skids *histories,
	unsigned int cars, const struct mh_software_skid_view *view,
	struct mh_error *error_out);
void			mh_software_skids_free(struct mh_software_skids *state);

#endif

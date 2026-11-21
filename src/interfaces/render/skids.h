#ifndef MH_INTERFACES_RENDER_SKIDS_H
#define MH_INTERFACES_RENDER_SKIDS_H

#include "engine/render/world_view.h"
#include "engine/simulation/skids.h"
#include "interfaces/render/lighting.h"

struct mh_world;

/*
 * Optional original-software skid submission for one lens frame. All inputs
 * are borrowed until the frame call returns. The view must describe the same
 * camera/lens as that frame. NULL world uses lighting's explicit light list;
 * otherwise the parsed immutable world supplies each ribbon's selected list.
 *
 * Optional output has cars rows of four wheel flags. It changes only after a
 * successful complete frame; history inputs are never changed. Apply these
 * flags to the simulation histories before the next skid update. Output must
 * not overlap any input. Passing NULL submission disables marks for the frame
 * and leaves the simulation's retained feedback alone.
 */
struct mh_renderer_skids {
	const struct mh_skids		*histories;
	unsigned int			cars;
	const struct mh_world_view	*view;
	const struct mh_world		*world;
	const struct mh_model_lighting	*lighting;
	int				(*oldest_visible)[4];
};

#endif

#ifndef MH_INTERFACES_RENDER_HALOS_H
#define MH_INTERFACES_RENDER_HALOS_H

#include <stddef.h>

#include "engine/render/halo.h"

struct mh_indexed_image;

/*
 * Original software source, borrowed for a lens frame. Endpoints/direction are
 * local to model (NULL means world coordinates). Style/color and visibility
 * eligibility are selected by the scene producer, before submission.
 *
 * Insert before the indicated primitive offset within the indicated scene part.
 * Offsets include hidden/culled primitives. Offset zero works for an empty
 * part; part == part_count with offset zero inserts after all parts, before
 * skids. Sources are ordered by part/offset, preserving source order at equal
 * positions. This retains original equal-depth ordering without folding halos
 * into meshes.
 */
struct mh_renderer_halo {
	size_t				part, before_primitive;
	const float			*model;
	float				endpoints[2][3], direction[3];
	unsigned int			flags;
	float				intensity, size, flare;
	unsigned char			color[3];
	int				tinted, lenses;
	float				tint[3];
	const struct mh_indexed_image	*image;
};

struct mh_renderer_halos {
	const struct mh_renderer_halo	*sources;
	size_t				count;
	const struct mh_indexed_image	*lens;

	/*
	 * Halo fade uses the configured cue start even when fog is off.
	 */
	float				cue_start;
};

#endif

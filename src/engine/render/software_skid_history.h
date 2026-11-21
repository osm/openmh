#ifndef MH_SOFTWARE_SKID_HISTORY_H
#define MH_SOFTWARE_SKID_HISTORY_H

#include "engine/render/world_view.h"
#include "engine/simulation/skids.h"

/*
 * Visit one wheel's count + partial segments in original ring order.
 * Coarse visibility uses the software world bounds, start sphere and focal
 * distance. The callback prepares an accepted segment and reports whether
 * any of its quads passed the pre-near-split face gates, even if clipping
 * subsequently produced no triangles. Inputs remain borrowed and immutable.
 *
 * oldest_visible is preserved for an empty history, otherwise reflects only
 * the oldest ring entry, even if culled. Output is unchanged on failure. A
 * callback may already have written to its context before a later failure;
 * callers must prepare into scratch and publish feedback only with a successful
 * frame.
 */
enum mh_error_code	mh_software_skid_visit(const struct mh_skid_wheel *,
	const struct mh_world_view *, float focal,
	enum mh_error_code (*prepare)(void *, const struct mh_skid_segment *, int *visible, struct mh_error *),
	void *context, int *oldest_visible, struct mh_error *error_out);

#endif

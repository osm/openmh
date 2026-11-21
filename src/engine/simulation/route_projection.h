#ifndef MH_ROUTE_PROJECTION_H
#define MH_ROUTE_PROJECTION_H

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/navigation/route.h"

struct mh_route_projection {
	size_t	nearest_key;
	double	parameter;

	/* Original forced-linear sample, including seam hold. */
	double	position[3];

	/* Route channel 3, before the audio [0,1] clamp. */
	double	sound_blend;
};

#define MH_ROUTE_CACHE_EMPTY MH_U32_MAX
/*
 * Binary32 projection. Supply EMPTY for an exhaustive search, otherwise the
 * retained nearest key (reduced modulo the number of nonclosing keys). On
 * success, output.nearest_key becomes the next cache value. Parsed route
 * required; failure preserves output.
 */
enum mh_error_code	mh_route_project_cached(const struct mh_route *route,
	const double position[3], unsigned long cache,
	struct mh_route_projection *output, struct mh_error *error_out);

/*
 * Double-precision no-cache reconstruction for placement/audio diagnostics;
 * race progress uses mh_route_project_cached above. No orientation or
 * relocation. Parsed route required. Output unchanged on error.
 */
enum mh_error_code	mh_route_project(const struct mh_route *route,
	const double position[3], struct mh_route_projection *output,
	struct mh_error *error_out);

#endif

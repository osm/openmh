#ifndef MH_PLACEMENT_H
#define MH_PLACEMENT_H

#include "engine/core/error.h"
#include "engine/navigation/route.h"
#include "interfaces/catalog/track.h"

struct mh_placement {
	double	position[3]; /* DISPLAY origin, original world units. */
	double	rotation[9];
	double	route_parameter; /* Raw MOT frame units. */
};

/* Staggered grid rules, with double route sampling. Slot limit 0..63. */
enum mh_error_code	mh_placement_grid(const struct mh_track *track,
	const struct mh_route *route, unsigned int slot,
	struct mh_placement *output, struct mh_error *error_out);

/*
 * No-cache relocation rules, with double route sampling and projection. Input
 * is the DISPLAY origin. Finds a nearest key and projects onto its neighboring
 * segments, preferring the preceding one; this is not a global nearest-curve
 * search. Does not collision-test the resulting pose.
 */
enum mh_error_code	mh_placement_recover(const struct mh_route *route,
	const double position[3], struct mh_placement *output,
	struct mh_error *error_out);

#endif

#ifndef MH_SOFTWARE_SKID_H
#define MH_SOFTWARE_SKID_H

#include "engine/simulation/skids.h"

struct mh_software_skid {
	unsigned int	quads;
	float	start[3], end[3], direction[3], extension[3], lateral[3];
	float		sample[3], step;
};

/*
 * Geometry, after the caller's visibility tests. Positions must be finite in
 * [-1e6,1e6], normals in [-2,2]. Degenerate segments succeed with quads=0.
 * Errors preserve output. No allocation. sample is the original point-light
 * query position, not the midpoint.
 */
enum mh_error_code	mh_software_skid_prepare(const struct mh_skid_segment *,
	struct mh_software_skid *output, struct mh_error *error_out);

/*
 * Read one cross-section of a successfully prepared, nondegenerate ribbon.
 * station must be in [0,quads]; outputs must be separate three-float arrays.
 * Quad q uses left(q), left(q+1), right(q+1), right(q), in that order.
 */
void	mh_software_skid_pair(const struct mh_software_skid *,
	unsigned int station, float left[3], float right[3]);

#endif

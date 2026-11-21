#ifndef MH_SOFTWARE_SHADOW_H
#define MH_SOFTWARE_SHADOW_H

#include "engine/render/shadow.h"

struct mh_software_shadow_query {
	/* Three road segments, lower endpoint first, then upper endpoint. */
	float	rays[MH_SHADOW_PROBES][2][3];
};

struct mh_software_shadow_shape {
	int	visible;
	float	points[MH_SHADOW_POINTS][3];
};

/*
 * Software shadow geometry. Use the shared default outline when points is NULL;
 * matrix is a column-major rotation then XYZ translation. Inputs must be finite
 * in [-1e6,1e6]. Failures preserve output. The caller first applies the
 * radius-15 sphere visibility test, then casts these road segments in order,
 * stopping at the first miss.
 */
enum mh_error_code	mh_software_shadow_prepare(const float *points,
	const float matrix[12], struct mh_software_shadow_query *output,
	struct mh_error *error_out);

/*
 * contacts contains three successful road hits in query order. A steep or
 * degenerate plane succeeds with visible=0. Software has no distance fade,
 * transmission colors or ground offset; its nine quads use mh_shadow_quads.
 */
enum mh_error_code	mh_software_shadow_project(const float *points,
	const float matrix[12], const float contacts[9],
	struct mh_software_shadow_shape *output, struct mh_error *error_out);

#endif

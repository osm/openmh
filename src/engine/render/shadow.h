#ifndef MH_SHADOW_H
#define MH_SHADOW_H

#include "engine/core/error.h"

enum {
	MH_SHADOW_POINTS = 16,
	MH_SHADOW_PROBES = 3,
	MH_SHADOW_QUADS = 9
};

/*
 * Car shadow geometry; platform/renderer independent. points is 16 packed XYZ
 * triples, or NULL for the default outline. matrix is a column-major 3x3
 * rotation followed by world translation. All inputs must be finite in
 * [-1e6,1e6]; errors preserve output.
 */
struct mh_shadow_query {
	int	visible;

	/* Cast from probe Y+1 to probe Y-10. */
	float	probes[MH_SHADOW_PROBES][3];

	/* Add to each original transmission byte, then truncate. */
	float	fade;
};

struct mh_shadow_shape {
	int		visible;
	float		points[MH_SHADOW_POINTS][3];

	/* Destination RGB multiplier / 255. */
	unsigned char	transmission[MH_SHADOW_POINTS];
};

/* Shared original outline for the software and Glide shadow producers. */
extern const float mh_shadow_default_points[MH_SHADOW_POINTS * 3];
extern const unsigned char mh_shadow_quads[MH_SHADOW_QUADS][4];
enum mh_error_code	mh_shadow_prepare(const float *points,
	const float matrix[12], const float eye[3],
	struct mh_shadow_query *output, struct mh_error *error_out);

/*
 * contacts are the three successful road-query positions, in query order.
 * Missing hits are handled by the caller. Steep/degenerate planes hide it.
 */
enum mh_error_code	mh_shadow_project(const float *points,
	const float matrix[12], const struct mh_shadow_query *query,
	const float *contacts, struct mh_shadow_shape *output,
	struct mh_error *error_out);

#endif

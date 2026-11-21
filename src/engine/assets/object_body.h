#ifndef MH_ASSETS_OBJECT_BODY_H
#define MH_ASSETS_OBJECT_BODY_H

#include "engine/animation/scene.h"
#include "engine/core/error.h"
#include "interfaces/geometry/model.h"

#define MH_OBJECT_BODY_POINTS 8

enum mh_object_body_flags {
	MH_OBJECT_HIGH_MASS = 1,
	MH_OBJECT_SLEEPING = 2
};

/*
 * Scenery physics fields, from visual MYO bounds and LWSC metadata. The
 * matching COL hull is loaded independently; it does not supply bounds.
 */
struct mh_object_body {
	double mass, inertia[3], radius, center[3],
		corners[MH_OBJECT_BODY_POINTS][3];

	/* Starts sleeping; mass >= 10000 also sets HIGH_MASS. */
	unsigned int	flags, sound_class;
};

/*
 * Presence of ShadowOptions finalizes an object; UnseenByRays suppresses its
 * body regardless of its value. Native parser retains these markers.
 */
int	mh_object_has_body(const struct mh_scene_object *object);

/*
 * Load scalar body properties. polygon_size is the decoded numeric value (zero
 * uses radius-derived mass). All stored values are binary32. Output is
 * unchanged on invalid/degenerate inputs.
 */
enum mh_error_code	mh_object_body_bounds(const double minimum[3],
	const double maximum[3], double polygon_size, unsigned int sound_class,
	struct mh_object_body *output, struct mh_error *error_out);
enum mh_error_code	mh_object_body_model(
	const struct mh_scene_object *object, const struct mh_model *model,
	struct mh_object_body *output, struct mh_error *error_out);

#endif

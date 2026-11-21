#include <math.h>
#include <string.h>

#include "engine/assets/object_body.h"
#include "engine/core/error.h"

#define BOUNDS_LIMIT 10000
#define POLYGON_SIZE_LIMIT 1e10
#define MASS_SCALE 100
#define RADIUS_MASS_FACTOR 12566.370616
#define HIGH_MASS_THRESHOLD 10000
#define MASS_LIMIT 1e18
#define INERTIA_LIMIT 1e27

enum {
	SOUND_CLASS_MAX = 15
};

int
mh_object_has_body(const struct mh_scene_object *object)
{
	return object != NULL && object->has_shadow_options &&
		!object->unseen_by_rays;
}

enum mh_error_code
mh_object_body_bounds(const double minimum[3], const double maximum[3],
	double polygon_size, unsigned int sound_class,
	struct mh_object_body *output, struct mh_error *error_out)
{
	struct mh_object_body body;
	double low[3], high[3], max_abs[3], extent[3];
	long double radius_squared;
	unsigned int axis, corner;

	if (minimum == NULL || maximum == NULL || output == NULL ||
		sound_class > SOUND_CLASS_MAX ||
		!(polygon_size >= 0 && polygon_size <= POLYGON_SIZE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid object body input");

	memset(&body, 0, sizeof(body));

	for (axis = 0; axis < 3; axis++) {
		if (!(minimum[axis] >= -BOUNDS_LIMIT &&
			minimum[axis] <= maximum[axis] &&
			maximum[axis] <= BOUNDS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid object model bounds");

		low[axis] = (double)(float)minimum[axis];
		high[axis] = (double)(float)maximum[axis];
		max_abs[axis] = fabs(low[axis]) > fabs(high[axis])
			? fabs(low[axis])
			: fabs(high[axis]);

		body.center[axis] =
			(double)(float)(((long double)high[axis] + low[axis]) *
				.5);
		extent[axis] =
			(double)(float)((long double)high[axis] - low[axis]);
	}

	radius_squared = (long double)max_abs[1] * max_abs[1] +
		(long double)max_abs[0] * max_abs[0];
	radius_squared += (long double)max_abs[2] * max_abs[2];
	body.radius = (double)(float)sqrt((double)radius_squared);
	body.mass = (double)(float)((long double)polygon_size * MASS_SCALE);

	if (body.mass == 0)
		body.mass = (double)(float)((long double)body.radius *
			RADIUS_MASS_FACTOR * body.radius * body.radius *
			(1.0 / 3));

	/*
	 * Square X and Y extents. Z remains linear in the following two sums.
	 */
	extent[0] = (double)(float)((long double)extent[0] * extent[0]);
	extent[1] = (double)(float)((long double)extent[1] * extent[1]);
	body.inertia[0] = (double)(float)(((long double)extent[1] + extent[2]) *
		body.mass * (1.0 / 12));
	body.inertia[1] = (double)(float)(((long double)extent[0] + extent[2]) *
		body.mass * (1.0 / 12));
	body.inertia[2] = (double)(float)(((long double)extent[0] + extent[1]) *
		body.mass * (1.0 / 12));

	if (!(body.mass > 0 && body.mass <= MASS_LIMIT) || !(body.radius > 0))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "degenerate object body");

	for (axis = 0; axis < 3; axis++)

		if (!(body.inertia[axis] > 0 &&
			body.inertia[axis] <= INERTIA_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"degenerate object inertia");

	for (corner = 0; corner < MH_OBJECT_BODY_POINTS; corner++)
		for (axis = 0; axis < 3; axis++)
			body.corners[corner][axis] =
				corner & (1U << axis) ? low[axis] : high[axis];

	body.flags = MH_OBJECT_SLEEPING |
		(body.mass >= HIGH_MASS_THRESHOLD
				? (unsigned int)MH_OBJECT_HIGH_MASS
				: 0U);
	body.sound_class = sound_class;
	*output = body;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_object_body_model(const struct mh_scene_object *object,
	const struct mh_model *model, struct mh_object_body *output,
	struct mh_error *error_out)
{
	if (!mh_object_has_body(object) || model == NULL ||
		model->vertex_count == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing object physics model");

	return mh_object_body_bounds(model->minimum, model->maximum,
		object->polygon_size, object->collision_sound_class, output,
		error_out);
}

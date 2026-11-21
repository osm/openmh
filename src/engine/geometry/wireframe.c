#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/geometry/wireframe.h"

#define COUNT_LIMIT 65536
#define COORDINATE_LIMIT 1000000
#define SHARED_SCALE_LIMIT 1.0e30f
#define FIT_SCALE_LIMIT 1.0e30L

void
mh_wireframe_free(struct mh_wireframe *mesh)
{
	if (mesh == NULL)
		return;

	free(mesh->points);
	free(mesh->edges);
	memset(mesh, 0, sizeof(*mesh));
}

static enum mh_error_code
find_center(const struct mh_wireframe *mesh, float center[3],
	struct mh_error *error_out)
{
	float low[3], high[3], value;
	unsigned int i, axis;

	if (mesh == NULL || mesh->points == NULL || mesh->point_count == 0 ||
		mesh->point_count > COUNT_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LOB fit");

	for (axis = 0; axis < 3; axis++) {
		low[axis] = COORDINATE_LIMIT;
		high[axis] = -COORDINATE_LIMIT;
	}

	for (i = 0; i < mesh->point_count; i++)
		for (axis = 0; axis < 3; axis++) {
			value = mesh->points[i].xyz[axis];

			if (!(value >= -COORDINATE_LIMIT &&
				value <= COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid LOB fit coordinate");

			if (value < low[axis])
				low[axis] = value;

			if (value > high[axis])
				high[axis] = value;
		}

	for (axis = 0; axis < 3; axis++)
		center[axis] =
			(float)(((long double)low[axis] + high[axis]) * 0.5);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_wireframe_center_scale(
	struct mh_wireframe *mesh, float scale, struct mh_error *error_out)
{
	float center[3], value;
	long double product;
	unsigned int i, axis;
	enum mh_error_code status;

	if (!(scale >= 0 && scale <= SHARED_SCALE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid LOB shared scale");

	status = find_center(mesh, center, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < mesh->point_count; i++)
		for (axis = 0; axis < 3; axis++) {
			value = (float)((long double)mesh->points[i].xyz[axis] -
				center[axis]);
			product = (long double)value * scale;

			if (!(product >= -COORDINATE_LIMIT &&
				product <= COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"LOB shared scale overflow");
		}

	for (i = 0; i < mesh->point_count; i++)
		for (axis = 0; axis < 3; axis++) {
			value = (float)((long double)mesh->points[i].xyz[axis] -
				center[axis]);
			mesh->points[i].xyz[axis] =
				(float)((long double)value * scale);
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_wireframe_fit(struct mh_wireframe *mesh, float radius, float *scale,
	struct mh_error *error_out)
{
	float center[3], extent, value, factor;
	long double quotient;
	unsigned int i, axis;
	enum mh_error_code status;

	if (scale == NULL || !(radius >= 0 && radius <= COORDINATE_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LOB fit");

	status = find_center(mesh, center, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	extent = 0;

	for (i = 0; i < mesh->point_count; i++)
		for (axis = 0; axis < 3; axis++) {
			value = (float)((long double)mesh->points[i].xyz[axis] -
				center[axis]);

			if (fabs(value) > extent)
				extent = (float)fabs(value);
		}

	quotient = extent == 0 ? 0 : (long double)radius / extent;

	/*
	 * Extremely small imported extents can overflow the original scale too;
	 * reject before modifying coordinates, rather than publish infinities.
	 */
	if (!(quotient >= 0 && quotient <= FIT_SCALE_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "LOB fit scale overflow");

	factor = (float)quotient;

	for (i = 0; i < mesh->point_count; i++)
		for (axis = 0; axis < 3; axis++) {
			value = (float)((long double)mesh->points[i].xyz[axis] -
				center[axis]);
			mesh->points[i].xyz[axis] = extent == 0
				? value
				: (float)((long double)value * factor);
		}

	*scale = factor;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

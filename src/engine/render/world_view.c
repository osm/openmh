#include <float.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/world_view.h"

static long double
transform_axis(const float pose[12], const float point[3], unsigned int i,
	enum mh_render_profile profile)
{
	if (profile == MH_RENDER_SOFTWARE)
		return (float)((float)((float)((float)(point[1] * pose[i + 3]) +
					(float)(point[0] * pose[i])) +
				(float)(point[2] * pose[i + 6])) +
			pose[i + 9]);

	return (((long double)point[1] * pose[i + 3] +
			(long double)point[0] * pose[i]) +
		(long double)point[2] * pose[i + 6]) +
		pose[i + 9];
}

enum mh_error_code
mh_world_view_setup_profile(enum mh_render_profile profile, unsigned int width,
	unsigned int height, float focal, float near_distance,
	float far_distance, const float pose[12], const float origin[3],
	const float spacing[2], const unsigned long dimensions[2],
	struct mh_world_view *output, struct mh_error *error_out)
{
	struct mh_world_view result;
	float point[3], minimum[2], maximum[2], aspect, vertical, inverse_focal,
		half_width, half_height;
	long double value, inverse, low, high;
	unsigned int axis, corner, world_axis;
	enum mh_error_code status;

	if (origin == NULL || spacing == NULL || dimensions == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing world visibility input");

	memset(&result, 0, sizeof(result));
	status = mh_frustum_setup_profile(profile, width, height, focal,
		near_distance, far_distance, pose, &result.frustum, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (axis = 0; axis < 3; axis++)
		if (!(origin[axis] >= -FLT_MAX && origin[axis] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid world visibility origin");

	for (axis = 0; axis < 2; axis++)
		if (!(spacing[axis] > 0 && spacing[axis] <= FLT_MAX) ||
			dimensions[axis] == 0 ||
			dimensions[axis] > (unsigned long)MH_I32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid world visibility grid");

	result.profile = profile;
	memcpy(result.camera, pose + 9, sizeof(result.camera));

	aspect = (float)((long double)width / height * .5L);
	vertical = (float)(1 / (long double)aspect);
	inverse_focal = (float)(1 / (long double)focal);

	for (corner = 0; corner < 8; corner++) {
		point[2] = corner < 4 ? near_distance : far_distance;
		half_width = (float)((long double)point[2] * inverse_focal);
		half_height = (float)((long double)point[2] * inverse_focal *
			vertical);
		point[0] = corner % 4 == 0 || corner % 4 == 3 ? -half_width
							: half_width;
		point[1] = corner % 4 < 2 ? half_height : -half_height;

		for (axis = 0; axis < 2; axis++) {
			world_axis = axis * 2;
			value = transform_axis(
					pose, point, world_axis, profile) -
				origin[world_axis];

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"world camera corner exceeds native range");

			result.corners[corner][axis] = (float)value;

			if (corner == 0 ||
				result.corners[corner][axis] < minimum[axis])
				minimum[axis] = result.corners[corner][axis];

			if (corner == 0 ||
				result.corners[corner][axis] > maximum[axis])
				maximum[axis] = result.corners[corner][axis];
		}
	}

	/*
	 * The original exits before the rectangle/bounds writes for tiny cells.
	 */
	if (spacing[0] < 1 || spacing[1] < 1) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (axis = 0; axis < 2; axis++) {
		value = (long double)minimum[axis] + origin[axis * 2];

		if (profile == MH_RENDER_SOFTWARE)
			value = (float)value;

		value -= spacing[axis];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"world visibility bounds exceed native range");

		result.bounds[axis * 2] = (float)value;
		value = (long double)maximum[axis] + origin[axis * 2];

		if (profile == MH_RENDER_SOFTWARE)
			value = (float)value;

		value += spacing[axis];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"world visibility bounds exceed native range");

		result.bounds[axis * 2 + 1] = (float)value;
		inverse = 1 / (long double)spacing[axis];

		if (profile == MH_RENDER_SOFTWARE) {
			inverse = (float)inverse;
			low = (float)((float)(minimum[axis] * inverse) - 1);
			high = (float)((float)(maximum[axis] * inverse) + 2);
		} else {
			low = minimum[axis] * inverse - 1;
			high = maximum[axis] * inverse + 2;
		}

		if (!(low >= -(long double)MH_U32_SIGN_BIT &&
			low <= (long double)MH_I32_MAX &&
			high >= -(long double)MH_U32_SIGN_BIT &&
			high <= (long double)MH_I32_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"world cell conversion exceeds int32 range");

		/*
		 * Truncate towards zero, then clamp to [0,dimension]. Avoid
		 * casting negative values to unsigned or assuming a native
		 * 32-bit long.
		 */
		result.first[axis] = low <= 0	  ? 0
			: low >= dimensions[axis] ? dimensions[axis]
						: (unsigned long)low;
		result.end[axis] = high <= 0	   ? 0
			: high >= dimensions[axis] ? dimensions[axis]
						: (unsigned long)high;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_world_view_setup(unsigned int width, unsigned int height, float focal,
	float near_distance, float far_distance, const float pose[12],
	const float origin[3], const float spacing[2],
	const unsigned long dimensions[2], struct mh_world_view *output,
	struct mh_error *error_out)
{
	return mh_world_view_setup_profile(MH_RENDER_GLIDE, width, height,
		focal, near_distance, far_distance, pose, origin, spacing,
		dimensions, output, error_out);
}

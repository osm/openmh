#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/frustum.h"

#define VALUE_LIMIT 1e9f
#define VIEWPORT_LIMIT 16384
#define FOCAL_MIN .5f
#define LENS_LIMIT 10
#define NEAR_MIN .1f
#define FAR_MIN 60
#define FAR_MAX 300

enum plane_index {
	NEAR_PLANE,
	FAR_PLANE,
	LEFT_PLANE,
	RIGHT_PLANE,
	BOTTOM_PLANE,
	TOP_PLANE
};

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

static void
normalize_vector(float vector[3])
{
	double squared;
	long double root, scale;
	unsigned int axis;

	squared = (double)(((long double)vector[1] * vector[1] +
				(long double)vector[0] * vector[0]) +
		(long double)vector[2] * vector[2]);

	if (squared == 0)
		return;

	root = sqrt(squared);
	root = (root + squared / root) * .5L;
	scale = 1 / root;

	for (axis = 0; axis < 3; axis++)
		vector[axis] = (float)(vector[axis] * scale);
}

static long double
dot(const float a[3], const float b[3], enum mh_render_profile profile)
{
	if (profile == MH_RENDER_SOFTWARE)
		return (float)((float)((float)(a[1] * b[1]) +
				(float)(a[0] * b[0])) +
			(float)(a[2] * b[2]));

	return ((long double)a[1] * b[1] + (long double)a[0] * b[0]) +
		(long double)a[2] * b[2];
}

static long double
rotate_axis(const float pose[12], const float point[3], unsigned int axis,
	enum mh_render_profile profile)
{
	if (profile == MH_RENDER_SOFTWARE)
		return (float)((float)((float)(point[1] * pose[axis + 3]) +
				(float)(point[0] * pose[axis])) +
			(float)(point[2] * pose[axis + 6]));

	return ((long double)point[1] * pose[axis + 3] +
		(long double)point[0] * pose[axis]) +
		(long double)point[2] * pose[axis + 6];
}

enum mh_error_code
mh_frustum_setup_profile(enum mh_render_profile profile, unsigned int width,
	unsigned int height, float focal, float near_distance,
	float far_distance, const float pose[12], struct mh_frustum *output,
	struct mh_error *error_out)
{
	struct mh_frustum result;
	float normals[MH_FRUSTUM_PLANES][3], corners[2][3], world[2][3];
	float aspect, vertical, inverse_focal, near_width, far_width,
		near_height, far_height;
	unsigned int component, axis, corner, plane, anchor;

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT || pose == NULL ||
		output == NULL || width < 1 || width > VIEWPORT_LIMIT ||
		height < 1 || height > VIEWPORT_LIMIT ||
		!(focal >= FOCAL_MIN && focal <= LENS_LIMIT) ||
		!(near_distance >= NEAR_MIN && near_distance <= LENS_LIMIT) ||
		!(far_distance >= FAR_MIN && far_distance <= FAR_MAX))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid object frustum");

	for (component = 0; component < 12; component++)
		if (!bounded(pose[component]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid frustum camera pose");

	aspect = (float)((long double)width / height * .5L);
	vertical = (float)(1 / (long double)aspect);
	inverse_focal = (float)(1 / (long double)focal);
	near_width = (float)((long double)near_distance * inverse_focal);
	far_width = (float)((long double)far_distance * inverse_focal);
	near_height =
		(float)((long double)near_distance * inverse_focal * vertical);
	far_height =
		(float)((long double)far_distance * inverse_focal * vertical);
	corners[0][0] = -near_width;
	corners[0][1] = near_height;
	corners[0][2] = near_distance;
	corners[1][0] = far_width;
	corners[1][1] = -far_height;
	corners[1][2] = far_distance;
	memset(normals, 0, sizeof(normals));
	normals[NEAR_PLANE][2] = -1;
	normals[FAR_PLANE][2] = 1;
	normals[LEFT_PLANE][0] =
		(float)((long double)near_distance - far_distance);
	normals[LEFT_PLANE][2] = (float)(-(long double)far_width + near_width);
	normalize_vector(normals[LEFT_PLANE]);
	normals[RIGHT_PLANE][0] = -normals[LEFT_PLANE][0];
	normals[RIGHT_PLANE][2] = normals[LEFT_PLANE][2];
	normals[BOTTOM_PLANE][1] =
		(float)((long double)near_distance - far_distance);
	normals[BOTTOM_PLANE][2] =
		(float)(-(long double)far_height + near_height);
	normalize_vector(normals[BOTTOM_PLANE]);
	normals[TOP_PLANE][1] = -normals[BOTTOM_PLANE][1];
	normals[TOP_PLANE][2] = normals[BOTTOM_PLANE][2];

	for (corner = 0; corner < 2; corner++)
		for (axis = 0; axis < 3; axis++)
			world[corner][axis] =
				(float)(rotate_axis(pose, corners[corner], axis,
						profile) +
					pose[9 + axis]);

	for (plane = 0; plane < MH_FRUSTUM_PLANES; plane++) {
		for (axis = 0; axis < 3; axis++)
			result.planes[plane][axis] = (float)rotate_axis(
				pose, normals[plane], axis, profile);

		anchor = plane == NEAR_PLANE || plane == LEFT_PLANE ||
				plane == TOP_PLANE
			? 0
			: 1;
		result.planes[plane][3] = (float)-dot(
			result.planes[plane], world[anchor], profile);

		for (component = 0; component < 4; component++)
			if (!bounded(result.planes[plane][component]))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"object frustum exceeds native range");
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_frustum_sphere_profile(enum mh_render_profile profile,
	const struct mh_frustum *frustum, const float position[3], float radius)
{
	unsigned int axis, plane, component;
	long double distance;

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT ||
		frustum == NULL || position == NULL ||
		!(radius >= 0 && radius <= VALUE_LIMIT))
		return 0;

	for (axis = 0; axis < 3; axis++)
		if (!bounded(position[axis]))
			return 0;

	for (plane = 0; plane < MH_FRUSTUM_PLANES; plane++) {
		for (component = 0; component < 4; component++)
			if (!bounded(frustum->planes[plane][component]))
				return 0;

		distance = dot(position, frustum->planes[plane], profile) +
			frustum->planes[plane][3];

		if (profile == MH_RENDER_SOFTWARE)
			distance = (float)distance;

		if (distance > radius)
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_frustum_setup(unsigned int width, unsigned int height, float focal,
	float near_distance, float far_distance, const float pose[12],
	struct mh_frustum *output, struct mh_error *error_out)
{
	return mh_frustum_setup_profile(MH_RENDER_GLIDE, width, height, focal,
		near_distance, far_distance, pose, output, error_out);
}

int
mh_frustum_sphere(
	const struct mh_frustum *frustum, const float position[3], float radius)
{
	return mh_frustum_sphere_profile(
		MH_RENDER_GLIDE, frustum, position, radius);
}

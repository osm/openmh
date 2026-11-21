#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/audio/a3d20_paths.h"

/* Native metric geometry tolerance and propagation speed. */
#define EPSILON .000001
#define SOUND_SPEED 343.3

static double
dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
subtract(const double a[3], const double b[3], double out[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		out[axis] = a[axis] - b[axis];
}

static double
distance(const double a[3], const double b[3])
{
	double delta[3];

	subtract(a, b, delta);

	return sqrt(dot(delta, delta));
}

static int
plane(const struct mh_a3d20_triangle *triangle, double origin[3], double u[3],
	double v[3], double normal[3])
{
	double length;
	unsigned int axis;

	for (axis = 0; axis < 3; axis++) {
		origin[axis] = triangle->vertices[0][axis];
		u[axis] = triangle->vertices[1][axis] - origin[axis];
		v[axis] = triangle->vertices[2][axis] - origin[axis];
	}

	normal[0] = u[1] * v[2] - u[2] * v[1];
	normal[1] = u[2] * v[0] - u[0] * v[2];
	normal[2] = u[0] * v[1] - u[1] * v[0];
	length = sqrt(dot(normal, normal));

	if (length <= EPSILON)
		return 0;

	for (axis = 0; axis < 3; axis++)
		normal[axis] /= length;

	return 1;
}

static int
intersect(const struct mh_a3d20_triangle *triangle, const double a[3],
	const double b[3], double *fraction)
{
	double origin[3], u[3], v[3], normal[3], direction[3], delta[3];
	double denominator, t, uu, uv, vv, du, dv, x, y;
	unsigned int axis;

	if (!plane(triangle, origin, u, v, normal))
		return 0;

	subtract(b, a, direction);
	denominator = dot(direction, normal);

	if (fabs(denominator) <= EPSILON)
		return 0;

	subtract(origin, a, delta);
	t = dot(delta, normal) / denominator;

	/* Endpoints on a reflecting surface must not occlude their own path. */
	if (t <= EPSILON || t >= 1 - EPSILON)
		return 0;

	for (axis = 0; axis < 3; axis++)
		delta[axis] = a[axis] + t * direction[axis] - origin[axis];

	uu = dot(u, u);
	uv = dot(u, v);
	vv = dot(v, v);
	du = dot(delta, u);
	dv = dot(delta, v);
	denominator = uu * vv - uv * uv;

	if (denominator <= EPSILON)
		return 0;

	x = (du * vv - dv * uv) / denominator;
	y = (dv * uu - du * uv) / denominator;

	if (x < -EPSILON || y < -EPSILON || x + y > 1 + EPSILON)
		return 0;

	*fraction = t;

	return 1;
}

static unsigned int
count_crossings(const struct mh_a3d20_geometry *geometry, const double a[3],
	const double b[3])
{
	double hits[MH_A3D20_TRIANGLES], t;
	size_t triangle_index;
	unsigned int hit_index, count;

	count = 0;

	for (triangle_index = 0; triangle_index < geometry->count;
		triangle_index++) {
		if (!intersect(&geometry->triangles[triangle_index], a, b, &t))
			continue;

		for (hit_index = 0; hit_index < count; hit_index++)
			if (fabs(t - hits[hit_index]) <= EPSILON)
				break;

		if (hit_index == count)
			hits[count++] = t;
	}

	return count;
}

static double
attenuation(double length, const struct mh_spatial_pcm_voice *source)
{
	if (length >= source->far_distance)
		return 0;

	if (length <= source->near_distance)
		return 1;

	return source->near_distance / length;
}

static double
pan(const double point[3], const struct mh_spatial_pcm_listener *listener)
{
	double direction[3], right[3], length, value;
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		direction[axis] = point[axis] - listener->position[axis];

	right[0] = (double)listener->up[1] * listener->front[2] -
		(double)listener->up[2] * listener->front[1];
	right[1] = (double)listener->up[2] * listener->front[0] -
		(double)listener->up[0] * listener->front[2];
	right[2] = (double)listener->up[0] * listener->front[1] -
		(double)listener->up[1] * listener->front[0];
	length = sqrt(dot(direction, direction) * dot(right, right));
	value = length > EPSILON ? dot(direction, right) / length : 0;

	return value < -1 ? -1 : value > 1 ? 1 : value;
}

static int
reflection(const struct mh_a3d20_triangle *triangle, const double source[3],
	const double listener[3], struct mh_a3d20_reflection *path,
	double *length)
{
	double origin[3], u[3], v[3], normal[3], delta[3], image[3];
	double source_side, listener_side, fraction;
	unsigned int axis;

	if (!plane(triangle, origin, u, v, normal))
		return 0;

	subtract(source, origin, delta);
	source_side = dot(delta, normal);
	subtract(listener, origin, delta);
	listener_side = dot(delta, normal);

	if (source_side * listener_side <= EPSILON)
		return 0;

	for (axis = 0; axis < 3; axis++)
		image[axis] = source[axis] - 2 * source_side * normal[axis];

	if (!intersect(triangle, listener, image, &fraction))
		return 0;

	for (axis = 0; axis < 3; axis++)
		path->point[axis] = listener[axis] +
			fraction * (image[axis] - listener[axis]);

	*length =
		distance(source, path->point) + distance(listener, path->point);

	return 1;
}

static void
retain_reflection(
	struct mh_a3d20_paths *paths, const struct mh_a3d20_reflection *path)
{
	unsigned int reflection_index, slot;

	/*
	 * Shared quad diagonals and repeated MYW cell references are one echo.
	 */
	for (reflection_index = 0; reflection_index < paths->count;
		reflection_index++)
		if (distance(path->point,
			paths->reflections[reflection_index].point) <=
			EPSILON)
			return;

	slot = paths->count;

	if (slot == MH_A3D20_REFLECTIONS) {
		slot = 0;

		for (reflection_index = 1; reflection_index < paths->count;
			reflection_index++)
			if (paths->reflections[reflection_index].gain <
				paths->reflections[slot].gain)
				slot = reflection_index;

		if (path->gain <= paths->reflections[slot].gain)
			return;
	} else
		paths->count++;

	paths->reflections[slot] = *path;
}

static int
vector_valid(const float vector[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		if (!(vector[axis] >= -MH_SOUND_POSITION_LIMIT &&
			vector[axis] <= MH_SOUND_POSITION_LIMIT))
			return 0;

	return 1;
}

static enum mh_error_code
validate_scene(const struct mh_a3d20_geometry *geometry,
	const struct mh_a3d20_material *material,
	const struct mh_spatial_pcm_listener *listener,
	const struct mh_spatial_pcm_voice *source, struct mh_a3d20_paths *paths,
	struct mh_error *error_out)
{
	unsigned int band;

	if (geometry == NULL || material == NULL || listener == NULL ||
		source == NULL || paths == NULL ||
		geometry->count > MH_A3D20_TRIANGLES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D acoustic scene");

	for (band = 0; band < 2; band++)
		if (!(material->reflectance[band] >= 0 &&
			material->reflectance[band] <= 1) ||
			!(material->transmittance[band] >= 0 &&
				material->transmittance[band] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid A3D material");

	if (!(material->reflection_gain >= 0 &&
		material->reflection_gain <= FLT_MAX) ||
		!(material->reflection_delay > 0 &&
			material->reflection_delay <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D reflection scales");

	if (!(source->near_distance >= 1 &&
		source->near_distance <= MH_SOUND_DISTANCE_MAX) ||
		!(source->far_distance >= source->near_distance &&
			source->far_distance <= MH_SOUND_DISTANCE_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D acoustic distances");

	if (!vector_valid(source->source.position) ||
		!vector_valid(listener->position) ||
		!vector_valid(listener->front) || !vector_valid(listener->up))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D acoustic vectors");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_a3d20_paths_build(const struct mh_a3d20_geometry *geometry,
	const struct mh_a3d20_material *material,
	const struct mh_spatial_pcm_listener *listener,
	const struct mh_spatial_pcm_voice *source, struct mh_a3d20_paths *paths,
	struct mh_error *error_out)
{
	struct mh_a3d20_paths next;
	struct mh_a3d20_reflection path;
	double emitter[3], receiver[3], direct, length, gain;
	size_t axis, wall_index, triangle_index;
	unsigned int walls;
	enum mh_error_code status;

	status = validate_scene(
		geometry, material, listener, source, paths, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.transmission = next.transmission_hf = 1;
	next.reflection_hf = material->reflectance[1];

	for (axis = 0; axis < 3; axis++) {
		emitter[axis] = source->source.position[axis];
		receiver[axis] = listener->position[axis];
	}

	walls = count_crossings(geometry, emitter, receiver);

	for (wall_index = 0; wall_index < walls; wall_index++) {
		next.transmission *= material->transmittance[0];
		next.transmission_hf *= material->transmittance[1];
	}

	direct = distance(emitter, receiver);
	gain = attenuation(direct, source);

	for (triangle_index = 0; gain > 0 && material->reflectance[1] > 0 &&
		triangle_index < geometry->count;
		triangle_index++) {
		if (!reflection(&geometry->triangles[triangle_index], emitter,
			receiver, &path, &length))
			continue;

		path.gain = attenuation(length, source) *
			material->reflectance[0] * material->reflection_gain;

		if (path.gain > 1)
			path.gain = 1;

		path.gain /= gain;

		if (path.gain == 0 ||
			count_crossings(geometry, emitter, path.point) ||
			count_crossings(geometry, receiver, path.point))
			continue;

		path.delay = (length - direct) / SOUND_SPEED *
			material->reflection_delay;

		if (path.delay < 0)
			path.delay = 0;

		if (path.delay > MH_A3D20_DELAY_MAX)
			path.delay = MH_A3D20_DELAY_MAX;

		path.pan = pan(path.point, listener);
		retain_reflection(&next, &path);
	}

	*paths = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

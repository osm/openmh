#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/body_points.h"
#include "engine/simulation/impact.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/pair.h"

#define CAR_MASS_LIMIT 10000
#define OBJECT_MASS_LIMIT 1e18
#define OBJECT_INERTIA_LIMIT 1e27
#define OBJECT_RADIUS_LIMIT 20000
#define COORDINATE_LIMIT 10000
#define CAR_INERTIA_X 3500
#define CAR_INERTIA_Y 2000
#define CAR_INERTIA_Z 1500
#define EXIT_DISTANCE_LIMIT 1000000
#define EXIT_DIRECTION_MINIMUM .01
#define INITIAL_DEPTH 10000
#define NORMAL_TOLERANCE .0001
#define COINCIDENT_DISTANCE 1e-12
#define CORRECTION_SCALE 1.2
#define IMPULSE_LIMIT 1e12
#define ANGULAR_IMPULSE_SCALE .1

/* Maps A-local vectors/points into B-local coordinates before correction. */
struct pair_transform {
	double	rotation[9], translation[3], direction[3];
};

struct pair_shape {
	double	mass, inertia[3], radius, points[MH_OBJECT_BODY_POINTS][3];
	unsigned int	point_count;
};

static double
dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
cross(const double a[3], const double b[3], double out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static void
rotate_vector(
	const double rotation[9], const double vector[3], double output[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		output[axis] = rotation[axis] * vector[0] +
			rotation[3 + axis] * vector[1] +
			rotation[6 + axis] * vector[2];
}

static void
unrotate_vector(
	const double rotation[9], const double vector[3], double output[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		output[axis] = dot(rotation + 3 * axis, vector);
}

static int
valid_car(const struct mh_car *car, const struct mh_collision *hull)
{
	unsigned int axis, corner;

	if (car == NULL || hull == NULL || hull->polygons == NULL ||
		hull->polygon_count == 0 ||
		hull->polygon_count > (unsigned long)MH_U16_MAX ||
		!car->has_corners || !car->has_suspension ||
		!(car->mass > 0 && car->mass <= CAR_MASS_LIMIT))
		return 0;

	for (axis = 0; axis < 3; axis++) {
		if (!(car->center_mass[axis] >= -COORDINATE_LIMIT &&
			car->center_mass[axis] <= COORDINATE_LIMIT))
			return 0;

		for (corner = 0; corner < MH_CAR_BASE_POINTS; corner++)
			if (!(car->corners[corner][axis] >= -COORDINATE_LIMIT &&
				car->corners[corner][axis] <=
					COORDINATE_LIMIT))
				return 0;
	}

	return 1;
}

static int
make_car_shape(const struct mh_car *car, const struct mh_collision *hull,
	unsigned int point_count, struct pair_shape *shape)
{
	double front_radius_squared, rear_radius_squared;

	if (!valid_car(car, hull) ||
		(point_count != MH_CAR_BASE_POINTS &&
			point_count != MH_CAR_BODY_POINTS))
		return 0;

	memset(shape, 0, sizeof(*shape));
	shape->point_count = point_count;
	shape->mass = car->mass;
	shape->inertia[0] = CAR_INERTIA_X;
	shape->inertia[1] = CAR_INERTIA_Y;
	shape->inertia[2] = CAR_INERTIA_Z;

	if (mh_car_body_points(car, shape->points, NULL) != MH_ERROR_NONE)
		return 0;

	front_radius_squared = car->corners[0][0] * car->corners[0][0] +
		car->corners[0][2] * car->corners[0][2];
	rear_radius_squared = car->corners[2][0] * car->corners[2][0] +
		car->corners[2][2] * car->corners[2][2];
	shape->radius = sqrt(front_radius_squared > rear_radius_squared
			? front_radius_squared
			: rear_radius_squared);

	return 1;
}

/*
 * Every hull half-space must contain the point. Polygon edge planes and the
 * octree are not used. Choose the first minimum exit distance. Caller clears
 * hit; earlier plane candidates may remain on a miss.
 */
static enum mh_error_code
query_hull(const struct mh_collision *hull, const double point[3],
	const double direction[3], struct mh_pair_contact *output,
	struct mh_error *error_out)
{
	struct mh_collision_polygon polygon;
	double side, along, distance, best;
	unsigned long polygon_index;
	int found;

	best = EXIT_DISTANCE_LIMIT;
	found = 0;

	for (polygon_index = 0; polygon_index < hull->polygon_count;
		polygon_index++) {
		if (hull->polygons == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid convex car hull query");

		polygon = hull->polygons[polygon_index];
		side = dot(polygon.plane, point) + polygon.plane[3];

		if (side > 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		along = dot(polygon.plane, direction);

		if (along <= 0)
			continue;

		if (along < EXIT_DIRECTION_MINIMUM)
			along = (double)(float)EXIT_DIRECTION_MINIMUM;

		distance = -side / along;

		if (distance < best) {
			best = distance;
			found = 1;
			output->polygon = polygon_index;
			memcpy(output->normal, polygon.plane,
				sizeof(output->normal));
		}
	}

	/*
	 * Original dereferences an unset plane for an unbounded/degenerate
	 * hull. Native behavior rejects that input instead.
	 */
	if (!found)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid convex car hull query");

	output->hit = 1;
	output->depth = -best;
	memcpy(output->point, point, sizeof(output->point));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
constrain_to_track(
	double vector[3], const struct mh_contact *contacts, unsigned int count)
{
	unsigned int i, j;
	double normal_component;

	if (contacts == NULL)
		return;

	for (i = 0; i < count; i++)
		if (contacts[i].hit) {
			normal_component = dot(vector, contacts[i].normal);

			if (normal_component < 0)
				for (j = 0; j < 3; j++)
					vector[j] -= contacts[i].normal[j] *
						normal_component;
		}
}

/* Caller supplies a zeroed contact; misses leave it unchanged. */
static enum mh_error_code
find_contact(const struct pair_shape *shape_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct pair_shape *shape_b, const struct mh_collision *hull_b,
	const struct mh_motion_state *b, struct pair_transform *transform,
	struct mh_pair_contact *contact, struct mh_error *error_out)
{
	struct mh_pair_contact best[2], hit;
	const struct pair_shape *shape;
	const struct mh_collision *hull;
	double origin_delta[3], reverse_direction[3], offset_in_b[3];
	double normal_in_b[3], corner[3], point[3];
	double length;
	unsigned int i, source, index;
	enum mh_error_code status;

	for (i = 0; i < 3; i++)
		origin_delta[i] = a->position[i] - b->position[i];

	length = sqrt(dot(origin_delta, origin_delta));

	if (length > shape_a->radius + shape_b->radius)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Relative transform maps A-local into B-local, before any correction.
	 */
	unrotate_vector(b->rotation, origin_delta, transform->translation);

	for (i = 0; i < 3; i++)
		unrotate_vector(b->rotation, a->rotation + 3 * i,
			transform->rotation + 3 * i);

	if (length > COINCIDENT_DISTANCE)
		for (i = 0; i < 3; i++)
			transform->direction[i] =
				transform->translation[i] / length;
	else {
		/*
		 * Deterministic world +X for coincident origins;
		 * original normalize has no usable direction. This is native
		 * singular-case policy.
		 */
		origin_delta[0] = 1;
		origin_delta[1] = origin_delta[2] = 0;
		unrotate_vector(
			b->rotation, origin_delta, transform->direction);
	}

	unrotate_vector(
		transform->rotation, transform->direction, reverse_direction);

	for (i = 0; i < 3; i++)
		reverse_direction[i] = -reverse_direction[i];

	memset(best, 0, sizeof(best));
	best[0].depth = best[1].depth = INITIAL_DEPTH;

	for (source = 0; source < 2; source++) {
		shape = source == 0 ? shape_a : shape_b;
		hull = source == 0 ? hull_b : hull_a;

		for (index = 0; index < shape->point_count; index++) {
			memcpy(corner, shape->points[index], sizeof(corner));

			if (source == 0) {
				rotate_vector(
					transform->rotation, corner, point);

				for (i = 0; i < 3; i++)
					point[i] += transform->translation[i];
			} else {
				for (i = 0; i < 3; i++)
					offset_in_b[i] = corner[i] -
						transform->translation[i];

				unrotate_vector(transform->rotation,
					offset_in_b, point);
			}

			memset(&hit, 0, sizeof(hit));
			status = query_hull(hull, point,
				source == 0 ? transform->direction
					: reverse_direction,
				&hit, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (!hit.hit || hit.depth >= best[source].depth)
				continue;

			hit.source = source;
			hit.corner = index;

			if (source != 0) {
				memcpy(hit.point, corner, sizeof(corner));
				rotate_vector(transform->rotation, hit.normal,
					normal_in_b);

				for (i = 0; i < 3; i++)
					hit.normal[i] = -normal_in_b[i];
			}

			best[source] = hit;
		}
	}

	if (!best[0].hit && !best[1].hit)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	*contact = best[0].depth < best[1].depth ? best[0] : best[1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_response(const struct pair_shape *shape_a,
	const struct mh_motion_state *a, const struct pair_shape *shape_b,
	const struct mh_motion_state *b, const struct pair_transform *transform,
	const struct mh_contact *contacts_b, int remote_b, int object_b,
	struct mh_pair_result *result, struct mh_error *error_out)
{
	double correction[3], arm_a[3], arm_b[3], arm_offset[3];
	double point_velocity_a[3], point_velocity_b[3], velocity_a_in_b[3];
	double spin_velocity[3], relative_velocity[3];
	double angular_response_a[3], angular_response_b[3];
	double rotational_response[3], rotational_response_b[3];
	double normal_world[3], constrained_normal[3];
	double linear_delta_a_in_b[3], angular_delta_a_in_b[3];
	double impulse, denominator;
	enum mh_error_code status;
	unsigned int i, participant, axis;

	for (i = 0; i < 3; i++)
		correction[i] = transform->direction[i] *
			result->contact.depth * CORRECTION_SCALE;

	rotate_vector(b->rotation, correction, result->correction);
	constrain_to_track(
		result->correction, contacts_b, shape_b->point_count);

	for (i = 0; i < 3; i++) {
		if (!object_b)
			result->state[0].position[i] -=
				result->correction[i] * (remote_b ? 1 : .5);

		if (!remote_b)
			result->state[1].position[i] +=
				result->correction[i] * (object_b ? 1 : .5);

		arm_b[i] = result->contact.point[i] - correction[i];
		arm_offset[i] = arm_b[i] - transform->translation[i];
	}

	unrotate_vector(transform->rotation, arm_offset, arm_a);

	/*
	 * The original uses arm CROSS angular velocity, and B's arm for BOTH
	 * inertia terms. Preserve this asymmetry; do not substitute rigid-body
	 * textbook formulas or subtract the CAR center of mass here.
	 */
	cross(arm_a, a->angular_velocity, spin_velocity);

	for (i = 0; i < 3; i++)
		point_velocity_a[i] = a->velocity[i] + spin_velocity[i];

	rotate_vector(transform->rotation, point_velocity_a, velocity_a_in_b);
	cross(arm_b, b->angular_velocity, spin_velocity);

	for (i = 0; i < 3; i++) {
		point_velocity_b[i] = b->velocity[i] + spin_velocity[i];
		relative_velocity[i] = velocity_a_in_b[i] - point_velocity_b[i];
	}

	cross(arm_b, result->contact.normal, angular_response_a);

	for (i = 0; i < 3; i++) {
		angular_response_b[i] =
			angular_response_a[i] / shape_b->inertia[i];
		angular_response_a[i] /= shape_a->inertia[i];
	}

	cross(angular_response_a, arm_b, rotational_response);
	denominator = 1 / shape_a->mass + 1 / shape_b->mass;

	if (object_b) {
		cross(angular_response_b, arm_b, rotational_response_b);

		for (i = 0; i < 3; i++)
			rotational_response[i] += rotational_response_b[i];

		denominator += dot(rotational_response, result->contact.normal);
	} else
		denominator +=
			2 * dot(rotational_response, result->contact.normal);

	impulse = -2 * dot(relative_velocity, result->contact.normal) /
		denominator;

	if (!(impulse >= -IMPULSE_LIMIT && impulse <= IMPULSE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"car impulse exceeds native bounds");

	result->impulse = impulse;
	status = mh_impact_severity(relative_velocity, result->contact.normal,
		&result->sound_severity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	rotate_vector(b->rotation, result->contact.normal, normal_world);
	constrain_to_track(normal_world, contacts_b, shape_b->point_count);
	unrotate_vector(b->rotation, normal_world, constrained_normal);

	for (i = 0; i < 3; i++) {
		linear_delta_a_in_b[i] =
			constrained_normal[i] * impulse / shape_a->mass;
		angular_delta_a_in_b[i] =
			angular_response_a[i] * impulse * ANGULAR_IMPULSE_SCALE;
		result->velocity_delta[1][i] =
			-constrained_normal[i] * impulse / shape_b->mass;
		result->angular_delta[1][i] = -angular_response_b[i] * impulse *
			ANGULAR_IMPULSE_SCALE;
	}

	unrotate_vector(transform->rotation, linear_delta_a_in_b,
		result->velocity_delta[0]);
	unrotate_vector(transform->rotation, angular_delta_a_in_b,
		result->angular_delta[0]);

	if (remote_b) {
		memset(result->velocity_delta[1], 0,
			sizeof(result->velocity_delta[1]));
		memset(result->angular_delta[1], 0,
			sizeof(result->angular_delta[1]));
	}

	for (participant = 0; participant < (remote_b ? 1U : 2U); participant++)
		for (axis = 0; axis < 3; axis++) {
			result->state[participant].velocity[axis] +=
				result->velocity_delta[participant][axis];
			result->state[participant].angular_velocity[axis] +=
				result->angular_delta[participant][axis];
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_pair(const struct pair_shape *shape_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct pair_shape *shape_b, const struct mh_collision *hull_b,
	const struct mh_motion_state *b, const struct mh_contact *contacts_b,
	int remote_b, int object_b, struct mh_pair_result *output,
	struct mh_error *error_out)
{
	struct mh_pair_result result;
	struct pair_transform transform;
	enum mh_error_code status;
	unsigned int i;

	if (a == NULL || b == NULL || output == NULL || !mh_motion_valid(a) ||
		!(object_b ? mh_motion_valid_object(b) : mh_motion_valid(b)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car contact input");

	if (contacts_b != NULL)
		for (i = 0; i < shape_b->point_count; i++) {
			if (contacts_b[i].hit != 0 && contacts_b[i].hit != 1)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid car contact constraint");

			if (contacts_b[i].hit &&
				!(fabs(dot(contacts_b[i].normal,
					contacts_b[i].normal) -
					1) <= NORMAL_TOLERANCE)) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid car contact constraint normal");
			}
		}

	memset(&result, 0, sizeof(result));
	result.state[0] = *a;
	result.state[1] = *b;

	status = find_contact(shape_a, hull_a, a, shape_b, hull_b, b,
		&transform, &result.contact, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!result.contact.hit) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = apply_response(shape_a, a, shape_b, b, &transform, contacts_b,
		remote_b, object_b, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!mh_motion_valid(&result.state[0]) ||
		!(object_b ? mh_motion_valid_object(&result.state[1])
			: mh_motion_valid(&result.state[1])))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"car contact exceeds motion bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_pair_step_points(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	unsigned int count_a, const struct mh_car *car_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	unsigned int count_b, const struct mh_contact *contacts_b, int remote_b,
	struct mh_pair_result *output, struct mh_error *error_out)
{
	struct pair_shape shape_a, shape_b;

	if ((remote_b != 0 && remote_b != 1) ||
		!make_car_shape(car_a, hull_a, count_a, &shape_a) ||
		!make_car_shape(car_b, hull_b, count_b, &shape_b))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car contact shape");

	return resolve_pair(&shape_a, hull_a, a, &shape_b, hull_b, b,
		contacts_b, remote_b, 0, output, error_out);
}

enum mh_error_code
mh_pair_step(const struct mh_car *car_a, const struct mh_collision *hull_a,
	const struct mh_motion_state *a, const struct mh_car *car_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	const struct mh_contact *contacts_b, struct mh_pair_result *output,
	struct mh_error *error_out)
{
	return mh_pair_step_points(car_a, hull_a, a, MH_CAR_BASE_POINTS, car_b,
		hull_b, b, MH_CAR_BASE_POINTS, contacts_b, 0, output,
		error_out);
}

enum mh_error_code
mh_pair_step_remote(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct mh_car *car_b, const struct mh_collision *hull_b,
	const struct mh_motion_state *b, const struct mh_contact *contacts_b,
	struct mh_pair_result *output, struct mh_error *error_out)
{
	return mh_pair_step_points(car_a, hull_a, a, MH_CAR_BASE_POINTS, car_b,
		hull_b, b, MH_CAR_BASE_POINTS, contacts_b, 1, output,
		error_out);
}

enum mh_error_code
mh_pair_step_object_points(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	unsigned int count_a, const struct mh_object_body *object_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	const struct mh_contact *contacts_b, struct mh_pair_result *output,
	struct mh_error *error_out)
{
	struct pair_shape shape_a, shape_b;
	unsigned int axis, corner;

	if (!make_car_shape(car_a, hull_a, count_a, &shape_a) ||
		object_b == NULL || hull_b == NULL ||
		hull_b->polygons == NULL || hull_b->polygon_count == 0 ||
		hull_b->polygon_count > (unsigned long)MH_U16_MAX ||
		!(object_b->mass > 0 && object_b->mass <= OBJECT_MASS_LIMIT) ||
		!(object_b->radius > 0 &&
			object_b->radius <= OBJECT_RADIUS_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid object contact shape");

	memset(&shape_b, 0, sizeof(shape_b));
	shape_b.point_count = MH_OBJECT_BODY_POINTS;
	shape_b.mass = object_b->mass;
	shape_b.radius = object_b->radius;

	for (axis = 0; axis < 3; axis++) {
		if (!(object_b->inertia[axis] > 0 &&
			object_b->inertia[axis] <= OBJECT_INERTIA_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid object contact inertia");

		shape_b.inertia[axis] = object_b->inertia[axis];

		for (corner = 0; corner < MH_OBJECT_BODY_POINTS; corner++) {
			if (!(object_b->corners[corner][axis] >=
					-COORDINATE_LIMIT &&
				object_b->corners[corner][axis] <=
					COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid object contact corner");

			shape_b.points[corner][axis] =
				object_b->corners[corner][axis];
		}
	}

	return resolve_pair(&shape_a, hull_a, a, &shape_b, hull_b, b,
		contacts_b, 0, 1, output, error_out);
}

enum mh_error_code
mh_pair_step_object(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct mh_object_body *object_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	const struct mh_contact *contacts_b, struct mh_pair_result *output,
	struct mh_error *error_out)
{
	return mh_pair_step_object_points(car_a, hull_a, a, MH_CAR_BASE_POINTS,
		object_b, hull_b, b, contacts_b, output, error_out);
}

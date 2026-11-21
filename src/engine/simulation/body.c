#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/simulation/body_internal.h"
#include "engine/simulation/contact_schedule.h"
#include "engine/simulation/motion_internal.h"

#define CAR_MASS_LIMIT 10000
#define CAR_STEP_LIMIT .1
#define OBJECT_MASS_LIMIT 1e18
#define OBJECT_INERTIA_LIMIT 1e27
#define OBJECT_STEP_LIMIT 1
#define COORDINATE_LIMIT 10000
#define CAR_INERTIA_X 5500
#define CAR_INERTIA_Y 5000
#define CAR_INERTIA_Z 3500
#define PENETRATION_SCALE 1.03
#define CAR_RESIDUAL_PUSH 50
#define CAR_VERTICAL_PUSH .2
#define INVERTED_UP_LIMIT -.5

struct body_shape {
	double	mass, inertia[3], center[3], points[MH_BODY_MAX_POINTS][3];
	unsigned int	point_count;
	int		is_car;
};

struct object_contact_context {
	const struct body_shape		*shape;
	const struct mh_collision	*col;
	const struct mh_motion_state	*entry;
	struct mh_body_result		*result;
	double				step_seconds;
};

static double
dot(const double a[3], const double b[3])
{
	volatile double value;

	/* Read floats and add Y + X + Z at binary53 precision. */
	value = (double)(float)a[1] * (double)(float)b[1];
	value += (double)(float)a[0] * (double)(float)b[0];
	value += (double)(float)a[2] * (double)(float)b[2];

	return value;
}

static void
cross(const double a[3], const double b[3], double out[3])
{
	unsigned int i, j, k;
	volatile double value, other;

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		value = (double)(float)a[j] * (double)(float)b[k];
		other = (double)(float)a[k] * (double)(float)b[j];
		out[i] = (float)(value - other);
	}
}

static void
rotate_to_local(
	const double rotation[9], const double vector[3], double output[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		output[axis] = (float)dot(rotation + axis * 3, vector);
}

static enum mh_error_code
make_car_shape(const struct mh_car *car, unsigned int point_count,
	struct body_shape *shape, struct mh_error *error_out)
{
	if (point_count != MH_CAR_BASE_POINTS &&
		point_count != MH_CAR_BODY_POINTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car body point count");

	memset(shape, 0, sizeof(*shape));
	shape->point_count = point_count;
	shape->is_car = 1;
	shape->mass = car->mass;
	shape->inertia[0] = CAR_INERTIA_X;
	shape->inertia[1] = CAR_INERTIA_Y;
	shape->inertia[2] = CAR_INERTIA_Z;
	memcpy(shape->center, car->center_mass, sizeof(shape->center));

	return mh_car_body_points(car, shape->points, error_out);
}

static enum mh_error_code
make_object_shape(const struct mh_object_body *object, struct body_shape *shape,
	struct mh_error *error_out)
{
	unsigned int axis, corner;

	memset(shape, 0, sizeof(*shape));
	shape->point_count = MH_BODY_MAX_POINTS;
	shape->mass = object->mass;

	for (axis = 0; axis < 3; axis++) {
		if (!(object->inertia[axis] > 0 &&
			object->inertia[axis] <= OBJECT_INERTIA_LIMIT) ||
			!(object->center[axis] >= -COORDINATE_LIMIT &&
				object->center[axis] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid object inertia or center");

		for (corner = 0; corner < MH_BODY_MAX_POINTS; corner++)
			if (!(object->corners[corner][axis] >=
					-COORDINATE_LIMIT &&
				object->corners[corner][axis] <=
					COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid object corner");
	}

	for (axis = 0; axis < 3; axis++)
		shape->inertia[axis] = (float)object->inertia[axis];

	memcpy(shape->center, object->center, sizeof(shape->center));
	memcpy(shape->points, object->corners, sizeof(shape->points));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
point_world(const struct body_shape *shape, unsigned int point,
	const struct mh_motion_state *state, double output[3])
{
	unsigned int axis;
	volatile double value;

	/* Stored inputs, Y + X + Z, then position. */
	for (axis = 0; axis < 3; axis++) {
		value = (double)(float)state->rotation[3 + axis] *
			(double)(float)shape->points[point][1];
		value += (double)(float)state->rotation[axis] *
			(double)(float)shape->points[point][0];
		value += (double)(float)state->rotation[6 + axis] *
			(double)(float)shape->points[point][2];
		value += (double)(float)state->position[axis];
		output[axis] = (float)value;
	}
}

static enum mh_error_code
find_impact(const struct body_shape *shape, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *state,
	struct mh_contact contacts[MH_BODY_MAX_POINTS],
	struct mh_body_impact *result, double best_start[3], double best_end[3],
	struct mh_error *error_out)
{
	struct mh_contact hit;
	double start[3], end[3];
	unsigned int corner;
	enum mh_error_code status;

	for (corner = 0; corner < shape->point_count; corner++) {
		point_world(shape, corner, entry, start);
		point_world(shape, corner, state, end);
		status = mh_collision_cast(col, start, end, &hit, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Copy only successful queries. A later miss in the same slice
		 * must retain this corner for the next car's pair response.
		 */
		if (hit.hit)
			contacts[corner] = hit;

		if (hit.hit &&
			(!result->contact.hit ||
				hit.fraction < result->contact.fraction)) {
			result->contact = hit;
			result->corner = corner;
			memcpy(best_start, start, sizeof(start));
			memcpy(best_end, end, sizeof(end));
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_impact(const struct body_shape *shape, const struct mh_collision *col,
	const struct mh_motion_state *entry, struct mh_motion_state *state,
	double step_seconds, struct mh_contact contacts[MH_BODY_MAX_POINTS],
	struct mh_body_impact *output, struct mh_error *error_out)
{
	struct mh_body_impact result;
	double best_start[3], best_end[3];
	double normal[3], arm[3], point_velocity[3], angular_response[3],
		rotational_response[3];
	double delta_rotation[9], rotation_delta[3], displacement[3],
		rebased_velocity[3];
	double penetration, inverse_mass, impulse, normal_y_squared;
	unsigned int i, j, k;
	volatile double value, other;
	enum mh_error_code status;

	memset(&result, 0, sizeof(result));

	status = find_impact(shape, col, entry, state, contacts, &result,
		best_start, best_end, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!result.contact.hit) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	rotate_to_local(state->rotation, result.contact.normal, normal);

	step_seconds = (float)step_seconds;

	for (i = 0; i < 3; i++)
		arm[i] =
			(float)((double)(float)shape->points[result.corner][i] -
				(double)(float)shape->center[i]);

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		value = (double)(float)state->angular_velocity[j] * arm[k];
		value += (double)(float)state->velocity[i];
		other = (double)(float)state->angular_velocity[k] * arm[j];
		point_velocity[i] = (float)(value - other);

		/* Store both products before subtraction. */
		value = (float)(arm[j] * normal[k]);
		other = (float)(arm[k] * normal[j]);
		angular_response[i] = (float)(value - other);
		angular_response[i] =
			(float)(angular_response[i] / shape->inertia[i]);
	}

	/* Store inverse mass, numerator and final impulse. */
	cross(angular_response, arm, rotational_response);
	inverse_mass = (float)(1 / (double)(float)shape->mass);
	value = (float)(-dot(point_velocity, normal));
	other = dot(rotational_response, normal);
	other += inverse_mass;
	impulse = (float)(value / other);
	result.impulse = impulse;
	value = (float)(inverse_mass * impulse);

	for (i = 0; i < 3; i++) {
		result.velocity_delta[i] = (float)(normal[i] * value);
		result.angular_delta[i] =
			(float)(angular_response[i] * impulse);
		displacement[i] = (float)(best_end[i] - best_start[i]);
	}

	value = -(1 - (double)(float)result.contact.fraction);
	value *= dot(displacement, result.contact.normal);
	value *= PENETRATION_SCALE;
	penetration = (float)value;

	/* Only car flag 4 adjusts these local components. */
	if (shape->is_car) {
		normal_y_squared = (double)(float)result.contact.normal[1] *
			(double)(float)result.contact.normal[1];
		result.velocity_delta[1] =
			(float)(result.velocity_delta[1] * normal_y_squared);
		result.angular_delta[0] =
			(float)(result.angular_delta[0] * normal_y_squared);
		result.angular_delta[2] =
			(float)(result.angular_delta[2] * normal_y_squared);

		if (result.velocity_delta[1] > 0)
			result.velocity_delta[1] =
				(float)sqrt(result.velocity_delta[1]);
	}

	/*
	 * Store the correction scalar/vector and updated pose. Then translate
	 * each local axis through binary32 arguments and pose stores. Rounding
	 * only the final position changes subsequent near-boundary collision
	 * decisions.
	 */
	for (i = 0; i < 3; i++) {
		result.correction[i] = (float)(penetration *
			(double)(float)result.contact.normal[i]);
		state->position[i] = (float)((double)(float)state->position[i] +
			result.correction[i]);
		state->velocity[i] = (float)((double)(float)state->velocity[i] +
			result.velocity_delta[i]);
		state->angular_velocity[i] =
			(float)((double)(float)state->angular_velocity[i] +
				result.angular_delta[i]);
		rotation_delta[i] = result.angular_delta[i] * step_seconds;

		displacement[i] = result.velocity_delta[i] * step_seconds;
	}

	/* Impact translates before rotating, then stores rebased velocity. */
	mh_motion_translate(state, displacement);
	memset(delta_rotation, 0, sizeof(delta_rotation));
	delta_rotation[0] = delta_rotation[4] = delta_rotation[8] = 1;
	mh_motion_rotate_pair(state->rotation, delta_rotation, rotation_delta);
	rotate_to_local(delta_rotation, state->velocity, rebased_velocity);
	memcpy(state->velocity, rebased_velocity, sizeof(rebased_velocity));
	result.normal_x = normal[0];
	result.forward = state->velocity[2];

	if (!(shape->is_car ? mh_motion_valid(state)
			: mh_motion_valid_object(state)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"body impact exceeds motion bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_begin(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out)
{
	unsigned int axis, corner;

	if (car == NULL || col == NULL || entry == NULL || current == NULL ||
		output == NULL ||
		(point_count != MH_CAR_BASE_POINTS &&
			point_count != MH_CAR_BODY_POINTS) ||
		!car->has_corners || !car->has_suspension ||
		!(car->mass > 0 && car->mass <= CAR_MASS_LIMIT) ||
		!(step_seconds > 0 && step_seconds <= CAR_STEP_LIMIT) ||
		!mh_motion_valid(entry) || !mh_motion_valid(current)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body configuration, state or timestep");
	}

	for (axis = 0; axis < 3; axis++) {
		if (!(car->center_mass[axis] >= -COORDINATE_LIMIT &&
			car->center_mass[axis] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body center");

		for (corner = 0; corner < MH_CAR_BASE_POINTS; corner++) {
			if (!(car->corners[corner][axis] >= -COORDINATE_LIMIT &&
				car->corners[corner][axis] <=
					COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid body corner");
		}
	}

	memset(output, 0, sizeof(*output));
	output->state = *current;
	output->point_count = point_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_impact_pass(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry, double step_seconds,
	struct mh_body_result *result, int *hit, struct mh_error *error_out)
{
	struct mh_body_impact impact;
	struct body_shape shape;
	enum mh_error_code status;

	if (result->impact_count >= MH_BODY_IMPACT_PASSES)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "body impact pass limit");

	status = make_car_shape(car, result->point_count, &shape, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = apply_impact(&shape, col, entry, &result->state, step_seconds,
		result->contacts, &impact, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*hit = impact.contact.hit;

	if (*hit) {
		result->impacts[result->impact_count++] = impact;
		result->feedback.contact = 1;
		result->feedback.material = impact.contact.material;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_residuals(const struct body_shape *shape,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	double step_seconds, struct mh_body_result *result,
	struct mh_error *error_out)
{
	struct mh_contact *contact;
	double start[3], end[3], normal[3], projection, normal_y_squared;
	double push[3], angles[3], delta_rotation[9], rebased[3];
	volatile double value;
	unsigned int i, j;
	enum mh_error_code status;

	/* Project sequentially and store each corner push. */
	step_seconds = (float)step_seconds;

	for (i = 0; i < shape->point_count; i++) {
		point_world(shape, i, entry, start);
		point_world(shape, i, &result->state, end);
		contact = &result->residual[i];
		status = mh_collision_cast(col, start, end, contact, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!contact->hit)
			continue;

		if (!result->feedback.contact)
			result->feedback.material = contact->material;

		result->feedback.contact = 1;
		result->residual_count++;
		rotate_to_local(
			result->state.rotation, contact->normal, normal);
		projection = dot(result->state.velocity, normal);

		for (j = 0; j < 3; j++) {
			value = normal[j] * projection;
			result->state.velocity[j] =
				(float)((double)(float)
						result->state.velocity[j] -
					value);
		}

		normal_y_squared = (double)(float)contact->normal[1] *
			(double)(float)contact->normal[1];

		if (shape->is_car && result->state.velocity[1] > 0)
			result->state.velocity[1] =
				(float)(result->state.velocity[1] *
					normal_y_squared);

		value = 1 - (double)(float)contact->fraction;

		if (shape->is_car)
			value *= CAR_RESIDUAL_PUSH;

		value = (float)value;

		for (j = 0; j < 3; j++)
			push[j] = (float)(normal[j] * value);

		if (shape->is_car) {
			value = normal_y_squared * CAR_VERTICAL_PUSH;
			push[1] = (float)(value * push[1]);
		}

		for (j = 0; j < 3; j++)
			result->push[j] = (float)(result->push[j] + push[j]);
	}

	/* Both residual routines explicitly clear the angular increment. */
	for (i = 0; i < 3; i++) {
		result->push[i] = (float)(result->push[i] * step_seconds);
		result->state.velocity[i] =
			(float)((double)(float)result->state.velocity[i] +
				result->push[i]);
		result->state.angular_velocity[i] =
			(float)((double)(float)
					result->state.angular_velocity[i] +
				0.0);
		angles[i] = 0;
	}

	mh_motion_translate(&result->state, result->push);
	memset(delta_rotation, 0, sizeof(delta_rotation));
	delta_rotation[0] = delta_rotation[4] = delta_rotation[8] = 1;
	mh_motion_rotate_pair(result->state.rotation, delta_rotation, angles);
	rotate_to_local(delta_rotation, result->state.velocity, rebased);
	memcpy(result->state.velocity, rebased, sizeof(rebased));

	result->feedback.strong_air_damping = shape->is_car &&
		result->feedback.contact &&
		entry->rotation[4] < INVERTED_UP_LIMIT;

	if (!(shape->is_car ? mh_motion_valid(&result->state)
			: mh_motion_valid_object(&result->state)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"body result exceeds motion bounds");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_finish(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry, double step_seconds,
	struct mh_body_result *result, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_body_residual(
		car, col, entry, step_seconds, result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_rotation_normalize(result->state.rotation);

	return mh_motion_valid(&result->state)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_RANGE,
			"body result exceeds motion bounds");
}

enum mh_error_code
mh_body_residual(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry, double step_seconds,
	struct mh_body_result *result, struct mh_error *error_out)
{
	struct body_shape shape;
	enum mh_error_code status;

	status = make_car_shape(car, result->point_count, &shape, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return resolve_residuals(
		&shape, col, entry, step_seconds, result, error_out);
}

static enum mh_error_code
object_contact(void *opaque, enum mh_contact_stage stage, unsigned int pass,
	int *hit, struct mh_error *error_out)
{
	struct object_contact_context *context;
	struct mh_body_result *result;
	struct mh_body_impact impact;
	enum mh_error_code status;

	(void)pass;
	context = (struct object_contact_context *)opaque;
	result = context->result;

	if (stage == MH_CONTACT_RESIDUAL)
		return resolve_residuals(context->shape, context->col,
			context->entry, context->step_seconds, result,
			error_out);

	status = apply_impact(context->shape, context->col, context->entry,
		&result->state, context->step_seconds, result->contacts,
		&impact, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*hit = impact.contact.hit;

	if (*hit) {
		result->impacts[result->impact_count++] = impact;
		result->feedback.contact = 1;
		result->feedback.material = impact.contact.material;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_object_frame(const struct mh_object_body *object,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	int sleeping, int *frame_impact, struct mh_body_result *output,
	struct mh_error *error_out)
{
	struct body_shape shape;
	struct mh_body_result result;
	struct object_contact_context context;
	enum mh_error_code status;

	if (object == NULL || col == NULL || entry == NULL || current == NULL ||
		output == NULL ||
		!(step_seconds > 0 && step_seconds <= OBJECT_STEP_LIMIT) ||
		!mh_motion_valid_object(entry) ||
		!mh_motion_valid_object(current) ||
		!(object->mass > 0 && object->mass <= OBJECT_MASS_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid object body arguments");

	status = make_object_shape(object, &shape, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.state = *current;
	result.point_count = MH_BODY_MAX_POINTS;
	context.shape = &shape;
	context.col = col;
	context.entry = entry;
	context.result = &result;
	context.step_seconds = step_seconds;
	status = mh_object_contact_schedule(
		sleeping, frame_impact, object_contact, &context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_object_step(const struct mh_object_body *object,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	struct mh_body_result *output, struct mh_error *error_out)
{
	int frame_impact;

	frame_impact = 0;

	return mh_body_object_frame(object, col, entry, current, step_seconds,
		0, &frame_impact, output, error_out);
}

static enum mh_error_code
body_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, int normalize, struct mh_body_result *output,
	struct mh_error *error_out)
{
	struct mh_body_result result;
	unsigned int pass;
	int hit;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing body output");

	status = mh_body_begin(car, col, entry, current, step_seconds,
		point_count, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (pass = 0; pass < MH_BODY_IMPACT_PASSES; pass++) {
		status = mh_body_impact_pass(car, col, entry, step_seconds,
			&result, &hit, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!hit)
			break;
	}

	status = (normalize ? mh_body_finish : mh_body_residual)(
		car, col, entry, step_seconds, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_step_points(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out)
{
	return body_step(car, col, entry, current, step_seconds, point_count, 1,
		output, error_out);
}

enum mh_error_code
mh_body_contact_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out)
{
	return body_step(car, col, entry, current, step_seconds, point_count, 0,
		output, error_out);
}

enum mh_error_code
mh_body_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	struct mh_body_result *output, struct mh_error *error_out)
{
	return mh_body_step_points(car, col, entry, current, step_seconds,
		MH_CAR_BASE_POINTS, output, error_out);
}

#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/ai_driver.h"

#define MASS_LIMIT 10000
#define CORNER_GRIP_MIN .01
#define CORNER_GRIP_LIMIT 100
#define CORNER_STEERING_LIMIT 100
#define BRAKE_POINTS_LIMIT 200000
#define LOCATION_LIMIT 3e6
#define THROTTLE_LIMIT_MIN .5
#define THROTTLE_LIMIT_MAX 1.25
#define REVERSE_SECONDS 2
#define HEADING_LIMIT 2
#define SPEED_LIMIT 1000
#define MIN_ROAD_WIDTH 1e-6
#define MIN_HEADING_LENGTH 1e-6
#define MIN_CORNER_GAIN 1e-6
#define STEERING_LIMIT 1e12

#define STRAIGHT_SPEED_SQUARED 100
#define BOOST_RADIUS_SCALE 1.5625
#define CORNER_GRAVITY 9.82
#define BRAKING_FORCE 28000
#define INITIAL_LANE_TOLERANCE .05
#define BRAKE_SPEED_FACTOR 1.52
#define BRAKE_COMMAND .89
#define ACCEL_SPEED_FACTOR .95
#define THROTTLE_RECOVERY_RATE .25
#define LANE_DECAY .2
#define REVERSE_EXIT_LATERAL 2
#define REVERSE_EXIT_HEADING .8
#define STUCK_SPEED 1.3888888888888888
#define LOW_SPEED 13.888888888888888
#define STEERING_GAIN .5
#define DAMPING_GAIN 3.9
#define CORNER_GAIN_SCALE .0015
#define MIN_DAMPING_SPEED 1e-9
#define DAMPING_LATERAL_LIMIT 5
#define HEADING_GAIN_LIMIT .9

/* Original section-specific braking exception. */
#define SPECIAL_SECTION 29
#define SPECIAL_RADIUS_MIN 150
#define SPECIAL_RADIUS_MAX 151
#define SPECIAL_SPEED 50

static int
valid_config(const struct mh_ai_driver_config *config)
{
	return config->mass >= 1 && config->mass <= MASS_LIMIT &&
		config->corner_grip >= CORNER_GRIP_MIN &&
		config->corner_grip <= CORNER_GRIP_LIMIT &&
		config->corner_steering >= 0 &&
		config->corner_steering <= CORNER_STEERING_LIMIT;
}

static int
valid_state(const struct mh_ai_driver_state *state)
{
	return state->brake_points >= 0 &&
		state->brake_points <= BRAKE_POINTS_LIMIT &&
		state->lane >= -LOCATION_LIMIT &&
		state->lane <= LOCATION_LIMIT &&
		state->previous_lateral >= -LOCATION_LIMIT &&
		state->previous_lateral <= LOCATION_LIMIT &&
		state->throttle_limit >= THROTTLE_LIMIT_MIN &&
		state->throttle_limit <= THROTTLE_LIMIT_MAX &&
		state->stuck_seconds >= -1 &&
		state->stuck_seconds <= REVERSE_SECONDS &&
		(state->turn_latch == -1 || state->turn_latch == 0 ||
			state->turn_latch == 1) &&
		(state->reversing == 0 || state->reversing == 1) &&
		(state->offroad == 0 || state->offroad == 1);
}

static int
valid_feedback(const struct mh_ai_driver_feedback *feedback)
{
	return feedback->heading_x >= -HEADING_LIMIT &&
		feedback->heading_x <= HEADING_LIMIT &&
		feedback->heading_z >= -HEADING_LIMIT &&
		feedback->heading_z <= HEADING_LIMIT &&
		feedback->speed >= -SPEED_LIMIT &&
		feedback->speed <= SPEED_LIMIT &&
		(feedback->boosted == 0 || feedback->boosted == 1) &&
		(feedback->wheelspin == 0 || feedback->wheelspin == 1);
}

static double
lateral_magnitude(const struct mh_ai_point *point, double x, double z,
	double *along, double *distance)
{
	double dx, dz, lateral_squared;

	dx = x - point->x;
	dz = z - point->z;
	*along = dx * point->direction[0] + dz * point->direction[2];
	*distance = sqrt(dx * dx + dz * dz);
	lateral_squared = (*distance) * (*distance) - (*along) * (*along);

	return lateral_squared > 0 ? sqrt(lateral_squared) : 0;
}

static double
target_speed_squared(double radius, double grip, int boosted)
{
	if (radius == 0)
		return STRAIGHT_SPEED_SQUARED;

	if (boosted)
		radius *= BOOST_RADIUS_SCALE;

	return fabs(radius) * CORNER_GRAVITY * grip;
}

static long
brake_distance(double target_squared, double speed, double deceleration)
{
	/*
	 * Cast truncates toward zero. Validated inputs stay in the signed-long
	 * range guaranteed by C89.
	 */
	return (long)((target_squared - speed * speed) / deceleration * .5);
}

enum mh_error_code
mh_ai_driver_start(const struct mh_ai_path *path, double x, double z,
	struct mh_ai_driver_state *output, struct mh_error *error_out)
{
	struct mh_ai_location location;
	struct mh_ai_driver_state state;
	double along, distance;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI driver output");

	status = mh_ai_locate(path, x, z, &location, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&state, 0, sizeof(state));
	state.point = location.point;
	state.corner = location.point;
	state.throttle_limit = 1;
	state.lane = lateral_magnitude(
		&path->points[state.point], x, z, &along, &distance);

	/*
	 * Original initialization uses unsigned lateral magnitude. Subsequent
	 * updates use signed lateral distance and an independent history slot.
	 */
	if (fabs(along - distance) < INITIAL_LANE_TOLERANCE)
		state.lane = 0;

	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
update_pedals(const struct mh_ai_path *path,
	const struct mh_ai_driver_state *state,
	const struct mh_ai_driver_feedback *feedback, double step_seconds,
	struct mh_ai_driver_result *result)
{
	const struct mh_ai_point *point, *corner;
	double speed, command;
	int can_accelerate;

	point = &path->points[result->state.point];
	corner = &path->points[result->state.corner];
	speed = feedback->speed;
	can_accelerate =
		point->radius == 0 || speed < result->point_target_speed;
	command = 0;

	/*
	 * Use the previous scan's braking distance. Point indices compare
	 * linearly even when the corner lies across the track seam.
	 */
	if (state->reversing)
		command = -.5;
	else if (corner->radius != 0 &&
		(long)result->state.corner - state->brake_points <=
			(long)result->state.point) {
		if (speed > result->corner_target_speed *
				(double)(float)BRAKE_SPEED_FACTOR)
			command = -(double)(float)BRAKE_COMMAND;
		else if (can_accelerate &&
			speed < result->corner_target_speed *
					(double)(float)ACCEL_SPEED_FACTOR)
			command = 1;
		else if (can_accelerate && speed < result->corner_target_speed)
			command = .5;
	} else if (can_accelerate)
		command = 1;

	/* The 2.2 fix guards short tracks before section 29. */
	if (path->section_count > SPECIAL_SECTION &&
		path->sections[SPECIAL_SECTION].radius > SPECIAL_RADIUS_MIN &&
		path->sections[SPECIAL_SECTION].radius < SPECIAL_RADIUS_MAX &&
		point->section == SPECIAL_SECTION && speed > SPECIAL_SPEED) {
		command = -(double)(float)BRAKE_COMMAND;
	}

	if (command > 0) {
		if (feedback->wheelspin) {
			command = 0;
			result->state.throttle_limit = .5;
		} else if (result->state.throttle_limit < 1) {
			result->state.throttle_limit +=
				step_seconds * THROTTLE_RECOVERY_RATE;

			if (result->state.throttle_limit < command)
				command = result->state.throttle_limit;
		}
	}

	if (command > 0)
		result->throttle = command;
	else if (command < 0)
		result->brake = -command;

	return command;
}

static enum mh_error_code
scan_corners(const struct mh_ai_path *path,
	const struct mh_ai_driver_config *config,
	const struct mh_ai_driver_feedback *feedback,
	struct mh_ai_driver_result *result, struct mh_error *error_out)
{
	const struct mh_ai_point *corner;
	double speed, deceleration, scan_length, target_squared;
	long current_start, candidate_start;
	size_t i, candidate;

	speed = feedback->speed;
	deceleration = -BRAKING_FORCE / config->mass;
	scan_length = speed * speed * .5 / -deceleration;

	/*
	 * Bound the scan to two passes over the point array. Check before
	 * converting to an integer or accessing any point.
	 */
	if (!(scan_length <
		(double)(2 * path->count - result->state.point - 1)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"AI scan exceeds path bound");

	result->scan_points = (size_t)scan_length;
	candidate = result->state.point;

	for (i = 0; i < result->scan_points; i++) {
		corner = &path->points[result->state.corner];

		if (path->points[candidate].radius != 0) {
			current_start = (long)result->state.corner -
				brake_distance(
					target_speed_squared(corner->radius,
						config->corner_grip,
						feedback->boosted),
					speed, deceleration);
			candidate_start = (long)candidate -
				brake_distance(
					target_speed_squared(
						path->points[candidate].radius,
						config->corner_grip,
						feedback->boosted),
					speed, deceleration);

			if (corner->radius == 0 ||
				candidate_start <= current_start)
				result->state.corner = candidate;
		}

		candidate = candidate + 1 == path->count ? 0 : candidate + 1;
	}

	corner = &path->points[result->state.corner];
	target_squared = target_speed_squared(
		corner->radius, config->corner_grip, feedback->boosted);

	if (corner->radius != 0 && target_squared < speed * speed) {
		result->state.brake_points =
			brake_distance(target_squared, speed, deceleration);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
update_reversing(const struct mh_ai_driver_state *state, double speed,
	double command, double step_seconds, struct mh_ai_driver_result *result)
{
	/*
	 * Pedals used the OLD reversing flag; steering below uses the NEW flag.
	 * The timer is not reset merely by speeding up.
	 */
	if (state->reversing) {
		if (state->stuck_seconds > 0)
			result->state.stuck_seconds -= step_seconds;
		else
			result->state.reversing = 0;

		if (fabs(result->lateral) < REVERSE_EXIT_LATERAL &&
			result->heading_dot > REVERSE_EXIT_HEADING) {
			result->state.reversing = 0;
			result->state.stuck_seconds = 0;
		}
	} else if (fabs(speed) < STUCK_SPEED && command > 0) {
		result->state.stuck_seconds += step_seconds;

		if (result->state.stuck_seconds > REVERSE_SECONDS) {
			result->state.stuck_seconds = REVERSE_SECONDS;
			result->state.reversing = 1;
		}
	}
}

static enum mh_error_code
steer_forward(const struct mh_ai_point *point,
	const struct mh_ai_section *section,
	const struct mh_ai_driver_config *config, double speed, int boosted,
	double step_seconds, struct mh_ai_driver_result *result,
	struct mh_error *error_out)
{
	double corner_gain, gain, damping_gain, feedforward_gain,
		target_squared;

	result->state.turn_latch = 0;
	result->lateral_rate =
		(result->lateral - result->state.previous_lateral) /
		step_seconds;
	result->state.previous_lateral = result->lateral;
	gain = STEERING_GAIN;
	damping_gain = (double)(float)DAMPING_GAIN;

	if (point->radius != 0) {
		corner_gain = 1 - fabs(section->radius) * CORNER_GAIN_SCALE;

		if (!(fabs(corner_gain) >= MIN_CORNER_GAIN))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"singular AI corner tuning");

		gain = corner_gain * STEERING_GAIN;
		damping_gain /= corner_gain;
		feedforward_gain = corner_gain * config->corner_steering;
		target_squared = target_speed_squared(
			(point->radius + section->radius) * .5,
			config->corner_grip, boosted);
		result->feedforward = speed * speed / target_squared *
			atan(feedforward_gain / sqrt(target_squared));

		if (section->radius < 0)
			result->feedforward = -result->feedforward;
	}

	/* Contact-plane projection can leave a tiny residual at zero speed. */
	if (fabs(speed) >= MIN_DAMPING_SPEED &&
		!(fabs(speed) < LOW_SPEED &&
			result->lateral > DAMPING_LATERAL_LIMIT)) {
		result->damping = damping_gain * result->lateral_rate / speed;
	}

	if (fabs(result->heading_dot) < HEADING_GAIN_LIMIT)
		gain *= 2 - fabs(result->heading_dot);

	result->correction = (result->state.lane - result->lateral) * gain /
		(point->width[0] + point->width[1]);
	result->steering =
		result->correction - result->damping + result->feedforward;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
apply_recovery_steering(const struct mh_ai_point *point, double speed,
	double command, struct mh_ai_driver_result *result)
{
	/* Low-speed off-road steering override. */
	if (speed < LOW_SPEED &&
		(result->lateral < -2 * point->width[0] ||
			result->lateral > 2 * point->width[1]))
		result->state.offroad = 1;

	if (result->state.offroad) {
		if (result->heading_dot > .5 || result->heading_dot < -.5) {
			if (command > 0)
				result->throttle = 1;

			result->steering = result->lateral < 0 ? 1 : -1;

			if (result->heading_dot < 0)
				result->steering = -result->steering;
		} else if (!result->state.reversing)
			result->steering = 0;

		if ((result->lateral < 0 &&
			-result->lateral < point->width[0]) ||
			(result->lateral > 0 &&
				result->lateral < point->width[1]))
			result->state.offroad = 0;
	}

	if (result->state.reversing)
		result->steering = -result->steering;
}

enum mh_error_code
mh_ai_driver_step_traffic(const struct mh_ai_path *path,
	const struct mh_ai_driver_config *config,
	const struct mh_ai_driver_state *state,
	const struct mh_ai_driver_feedback *feedback, double step_seconds,
	const struct mh_ai_traffic_result *traffic,
	struct mh_ai_driver_result *output, struct mh_error *error_out)
{
	struct mh_ai_driver_result result;
	struct mh_ai_cursor cursor;
	const struct mh_ai_point *point, *corner;
	const struct mh_ai_section *section;
	enum mh_error_code status;
	double length, heading_x, heading_z, heading_cross, along, distance,
		speed, command;

	if (path == NULL || path->sections == NULL || path->section_count < 1 ||
		path->section_count > MH_AI_MAX_SECTIONS || config == NULL ||
		state == NULL || feedback == NULL || output == NULL ||
		!(step_seconds > 0 && step_seconds <= 1) ||
		!valid_config(config) || !valid_state(state) ||
		!valid_feedback(feedback)) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid AI driver input");
	}

	if (traffic != NULL &&
		(!(traffic->lane >= -LOCATION_LIMIT &&
			traffic->lane <= LOCATION_LIMIT) ||
			(traffic->brake != 0 && traffic->brake != 1) ||
			(traffic->changing != 0 && traffic->changing != 1)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI traffic decision");

	status = mh_ai_follow(path, feedback->x, feedback->z, state->point,
		state->corner, &cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	point = &path->points[cursor.point];
	corner = &path->points[cursor.corner];

	if (point->section >= path->section_count ||
		!(point->width[0] + point->width[1] >= MIN_ROAD_WIDTH)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI road section or width");
	}

	section = &path->sections[point->section];
	length = sqrt(feedback->heading_x * feedback->heading_x +
		feedback->heading_z * feedback->heading_z);

	if (!(length >= MIN_HEADING_LENGTH))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "degenerate AI heading");

	heading_x = feedback->heading_x / length;
	heading_z = feedback->heading_z / length;
	memset(&result, 0, sizeof(result));
	result.state = *state;
	result.state.point = cursor.point;
	result.state.corner = cursor.corner;
	result.heading_dot = heading_x * point->direction[0] +
		heading_z * point->direction[2];
	heading_cross = point->direction[0] * heading_z -
		point->direction[2] * heading_x;
	result.lateral = lateral_magnitude(
		point, feedback->x, feedback->z, &along, &distance);

	if ((feedback->z - point->z) * point->direction[0] -
			(feedback->x - point->x) * point->direction[2] >
		0) {
		result.lateral = -result.lateral;
	}

	speed = feedback->speed;
	result.point_target_speed = sqrt(target_speed_squared(
		point->radius, config->corner_grip, feedback->boosted));
	result.corner_target_speed = sqrt(target_speed_squared(
		corner->radius, config->corner_grip, feedback->boosted));
	command = update_pedals(path, state, feedback, step_seconds, &result);

	status = scan_corners(path, config, feedback, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (traffic == NULL)
		result.state.lane -=
			result.state.lane * (double)(float)LANE_DECAY;
	else {
		result.state.lane = traffic->lane;

		if (traffic->brake) {
			result.throttle = 0;
			result.brake = 1;
		}
	}

	update_reversing(state, speed, command, step_seconds, &result);

	if (result.heading_dot < 0) {
		if (result.state.turn_latch == 0) {
			/* Compare RIGHT clearance against LEFT. */
			result.state.turn_latch =
				point->width[1] - result.lateral <
					point->width[0] + result.lateral
				? 1
				: -1;
		}

		/*
		 * Original first latch-setting branch leaves a stack temporary
		 * stale. Native policy applies the selected latch immediately.
		 */
		result.steering = result.state.turn_latch;
	} else {
		status = steer_forward(point, section, config, speed,
			feedback->boosted, step_seconds, &result, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (traffic != NULL && traffic->changing)
			result.steering *= 2;

		if (fabs(result.heading_dot) < .5)
			result.steering = heading_cross > 0 ? 1 : -1;
	}

	apply_recovery_steering(point, speed, command, &result);

	if (!(result.steering >= -STEERING_LIMIT &&
		result.steering <= STEERING_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"AI steering exceeds native bound");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_driver_step(const struct mh_ai_path *path,
	const struct mh_ai_driver_config *config,
	const struct mh_ai_driver_state *state,
	const struct mh_ai_driver_feedback *feedback, double step_seconds,
	struct mh_ai_driver_result *output, struct mh_error *error_out)
{
	return mh_ai_driver_step_traffic(path, config, state, feedback,
		step_seconds, NULL, output, error_out);
}

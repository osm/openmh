#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/ai_traffic.h"

#define LOCATION_LIMIT 3e6
#define SPEED_LIMIT 1000
#define HEADING_LIMIT 2
#define LATERAL_RATE_LIMIT 6e12
#define CAR_EXTENT_LIMIT 100
#define ROAD_WIDTH_LIMIT 1e6
#define SIDE_POINT_DISTANCE 5
#define LOOKAHEAD_SECONDS 5
#define LANE_STEP .2
#define CLEARANCE_MARGIN .1
#define SPEED_MATCH_FACTOR 1.01

static int
valid_car(const struct mh_ai_traffic_car *car, size_t point_count)
{
	return car->point < point_count && car->along >= -LOCATION_LIMIT &&
		car->along <= LOCATION_LIMIT &&
		car->lateral >= -LOCATION_LIMIT &&
		car->lateral <= LOCATION_LIMIT &&
		car->lane >= -LOCATION_LIMIT && car->lane <= LOCATION_LIMIT &&
		car->speed >= -SPEED_LIMIT && car->speed <= SPEED_LIMIT &&
		car->heading_dot >= -HEADING_LIMIT &&
		car->heading_dot <= HEADING_LIMIT &&
		car->lateral_rate >= -LATERAL_RATE_LIMIT &&
		car->lateral_rate <= LATERAL_RATE_LIMIT && car->width > 0 &&
		car->width <= CAR_EXTENT_LIMIT && car->length > 0 &&
		car->length <= CAR_EXTENT_LIMIT;
}

static void
mark_blocked_sides(const struct mh_ai_traffic_car *cars, unsigned int count,
	unsigned int self, double lane_step, double margin,
	struct mh_ai_traffic_result *result)
{
	const struct mh_ai_traffic_car *self_car, *other;
	double lateral_gap, clearance;
	long point_delta;
	unsigned int i, other_index;

	self_car = &cars[self];

	/*
	 * Clear both flags once, not on each candidate. This scan deliberately
	 * uses absolute LINEAR point difference at the seam.
	 */
	for (i = 1; i < count; i++) {
		other_index = (self + i) % count;
		other = &cars[other_index];
		point_delta = (long)self_car->point - (long)other->point;

		if (point_delta <= -SIDE_POINT_DISTANCE ||
			point_delta >= SIDE_POINT_DISTANCE)
			continue;

		lateral_gap = self_car->lateral - other->lateral;
		clearance = (self_car->width + other->width) * .5 + margin +
			lane_step;

		if (lateral_gap > 0 && lateral_gap < clearance)
			result->blocked_left = 1;

		if (lateral_gap < 0 && -lateral_gap < clearance)
			result->blocked_right = 1;
	}
}

enum mh_error_code
mh_ai_traffic_step(const struct mh_ai_path *path,
	const struct mh_ai_traffic_car *cars, unsigned int count,
	unsigned int self, int avoid_sides, struct mh_ai_traffic_result *output,
	struct mh_error *error_out)
{
	const struct mh_ai_traffic_car *self_car, *other;
	const struct mh_ai_point *point;
	struct mh_ai_traffic_result result;
	double other_span, clearance, overlap, lane_step, margin;
	long point_delta, lookahead_points;
	unsigned int i, other_index;
	int ahead, seen_ahead, can_pass_left, can_pass_right;

	if (path == NULL || path->points == NULL || path->count < 1 ||
		path->count > MH_AI_MAX_POINTS || cars == NULL || count < 1 ||
		count > MH_TRAFFIC_MAX_CARS || self >= count ||
		output == NULL || (avoid_sides != 0 && avoid_sides != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI traffic input");

	for (i = 0; i < count; i++) {
		other = &cars[i];

		if (!valid_car(other, path->count)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid AI traffic car");
		}
	}

	self_car = &cars[self];
	point = &path->points[self_car->point];

	if (!(point->width[0] >= 0 && point->width[0] <= ROAD_WIDTH_LIMIT) ||
		!(point->width[1] >= 0 && point->width[1] <= ROAD_WIDTH_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI traffic road width");

	memset(&result, 0, sizeof(result));
	result.threat = -1;
	lane_step = (double)(float)LANE_STEP;
	margin = (double)(float)CLEARANCE_MARGIN;

	if (avoid_sides)
		mark_blocked_sides(
			cars, count, self, lane_step, margin, &result);

	seen_ahead = 0;

	for (i = 1; i < count; i++) {
		other_index = (self + count - i) % count;
		other = &cars[other_index];
		other_span = (1 - other->heading_dot) * other->length +
			other->width * other->heading_dot;
		clearance = other_span * .5 + self_car->width + margin + margin;
		lookahead_points = (long)((self_car->speed - other->speed) *
			LOOKAHEAD_SECONDS);

		if (lookahead_points < 0)
			continue;

		point_delta = (long)other->point - (long)self_car->point;

		/*
		 * Compare as UNSIGNED. A negative point delta must be below
		 * lookahead_points-count; preserve this counterintuitive seam.
		 */
		ahead = (point_delta > 0 && point_delta < lookahead_points) ||
			(point_delta < 0 &&
				lookahead_points < (long)path->count &&
				point_delta <
					lookahead_points - (long)path->count);

		if (ahead) {
			seen_ahead = 1;

			if (other->speed * (double)(float)SPEED_MATCH_FACTOR >=
				self_car->speed)
				continue;

			overlap = (self_car->width + other_span) * .5 + margin;

			if (fabs(other->lateral - self_car->lateral) >= overlap)
				continue;

			result.threat = (int)other_index;
			can_pass_right =
				point->width[1] - other->lateral > clearance &&
				point->width[1] - other->lane - result.shift >
					clearance &&
				!result.blocked_right;
			can_pass_left =
				point->width[0] + other->lateral > clearance &&
				point->width[0] + other->lane + result.shift >
					clearance &&
				!result.blocked_left;

			if (other->lateral < self_car->lateral) {
				if (can_pass_right)
					result.shift += lane_step;
				else if (can_pass_left)
					result.shift -= lane_step;
				else
					result.brake = 1;
			} else if (other->lateral > self_car->lateral) {
				if (can_pass_left)
					result.shift -= lane_step;
				else if (can_pass_right)
					result.shift += lane_step;
				else
					result.brake = 1;
			}

			/* Equal lateral: original neither steers nor brakes. */
			break;
		}

		if (point_delta != 0 || seen_ahead)
			continue;

		if (other->lateral < self_car->lateral &&
			self_car->lateral_rate < 0 &&
			point->width[1] - self_car->lane - result.shift >
				clearance)
			result.shift += lane_step;
		else if (other->lateral > self_car->lateral &&
			self_car->lateral_rate > 0 &&
			point->width[0] + self_car->lane + result.shift >
				clearance)
			result.shift -= lane_step;
	}

	result.changing = result.shift != 0;
	result.lane = result.changing
		? self_car->lane + result.shift
		: self_car->lane - self_car->lane * lane_step;

	if (!(result.lane >= -LOCATION_LIMIT && result.lane <= LOCATION_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"AI traffic lane exceeds native bound");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

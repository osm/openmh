#ifndef MH_AI_TRAFFIC_H
#define MH_AI_TRAFFIC_H

#include "engine/core/error.h"
#include "engine/navigation/ai_path.h"

#define MH_TRAFFIC_MAX_CARS 8

/*
 * Road-local samples in retained traversal order. Width/length are full
 * extents, not half extents. Original loader uses 2.1/4.4 for every player.
 */
struct mh_ai_traffic_car {
	size_t	point;
	double	along, lateral, lane, speed, heading_dot, lateral_rate;
	double	width, length;
};

struct mh_ai_traffic_result {
	double	lane, shift;
	int	blocked_left, blocked_right, brake, changing;

	/* Index in the supplied order, or -1. */
	int	threat;
};

/*
 * Others are scanned circularly in both directions. avoid_sides supplies the
 * original random-profile comparison as a boolean. No sorting, allocation,
 * globals or car-to-car impulses. Output is atomic.
 */
enum mh_error_code	mh_ai_traffic_step(const struct mh_ai_path *path,
	const struct mh_ai_traffic_car *cars, unsigned int count,
	unsigned int self, int avoid_sides, struct mh_ai_traffic_result *output,
	struct mh_error *error_out);

#endif

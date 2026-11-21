#ifndef MH_AI_DRIVER_H
#define MH_AI_DRIVER_H

#include "engine/core/error.h"
#include "engine/simulation/ai_path.h"
#include "engine/simulation/ai_traffic.h"

struct mh_ai_driver_config {
	double	mass;
	double	corner_grip;	/* Selected TRK AI1/2/3 value 8 (zero based). */
	double	corner_steering; /* Selected TRK AI1/2/3 value 9. */
};

struct mh_ai_driver_state {
	size_t	point, corner;
	long	brake_points;
	double	lane, previous_lateral, throttle_limit;
	double	stuck_seconds, turn_latch;
	int	reversing, offroad;
};

struct mh_ai_driver_feedback {
	double	x, z; /* Display position, not the internal center of mass. */

	/* Display forward axis; normalized here. */
	double	heading_x, heading_z;
	double	speed; /* Local Z velocity, original world units per second. */
	int	boosted, wheelspin;
};

struct mh_ai_driver_result {
	struct mh_ai_driver_state	state;

	/* Raw steering; vehicle clamps it. */
	double				throttle, brake, steering;
	double				lateral, heading_dot, lateral_rate;

	/* Corner-derived pedal targets, evaluated before the new scan. */
	double				point_target_speed, corner_target_speed;
	double				correction, damping, feedforward;
	size_t				scan_points;
};

/*
 * The driver borrows a fixed path for the session. The caller supplies
 * random corner boosts. Outputs are unchanged on failure.
 */
enum mh_error_code	mh_ai_driver_start(const struct mh_ai_path *path,
	double x, double z, struct mh_ai_driver_state *output,
	struct mh_error *error_out);
enum mh_error_code	mh_ai_driver_step(const struct mh_ai_path *path,
	const struct mh_ai_driver_config *config,
	const struct mh_ai_driver_state *state,
	const struct mh_ai_driver_feedback *feedback, double step_seconds,
	struct mh_ai_driver_result *output, struct mh_error *error_out);

/* Optional traffic decision is applied between corner scanning and recovery. */
enum mh_error_code	mh_ai_driver_step_traffic(const struct mh_ai_path *path,
	const struct mh_ai_driver_config *config,
	const struct mh_ai_driver_state *state,
	const struct mh_ai_driver_feedback *feedback, double step_seconds,
	const struct mh_ai_traffic_result *traffic,
	struct mh_ai_driver_result *output, struct mh_error *error_out);

#endif

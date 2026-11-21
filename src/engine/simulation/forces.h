#ifndef MH_FORCES_H
#define MH_FORCES_H

#include "engine/core/error.h"
#include "engine/simulation/motion.h"

struct mh_force_input {
	/* Clamped to [-1,1] before physical force calculation. */
	double	steering;

	/* Steering smoothing is NOT physical steering. */
	double	previous_steering;

	/* Return, before doubling. Produced by mh_engine_step. */
	double	longitudinal;

	/* Phase for the wheelspin branch's sine argument. */
	double	phase;

	/* Previous detachment state inhibits reversal of steering. */
	int	sliding;

	/* Wheelspin after engine update selects force split. */
	int	wheelspin;

	/* Reduced-gravity flag halves ordinary gravity. */
	int	reduced_gravity;
};

struct mh_force_result {
	struct mh_motion_forces	forces;

	/* Clamped input. */
	double			steering;
	double			smoothed_steering;

	/* Signed speed-ramped value before wheel split. */
	double			steering_force;

	/* Ordinary 15.8100004196167, or half. */
	double			gravity;
};

/*
 * Upstream wheel-force preparation, excluding its engine call. Uses tick-entry
 * rotation and local forward velocity, before motion's previous-tick
 * correction. Gravity and speed-dependent world-Y load are unconditional;
 * steering/drive forces require suspension support later. Inputs, world/local
 * loads, steering history and final wheel vectors retain binary32 stores with
 * binary53 arithmetic between them. Result is BEFORE material X scaling /
 * weight-transfer adjustments.
 *
 * Supplying it directly to mh_motion_step is an explicit partial simulation,
 * not complete tire/engine behavior. mh_grip_step supplies material
 * adjustment/damping in the proper order. mh_vehicle_step connects the engine
 * and feedback to both stages.
 *
 * Requires car mass/PhyTurnForce and a valid motion state. Input limits:
 * steering <= 1e12 in magnitude, previous steering in [-1,1], longitudinal <=
 * 1e8, phase <= 1e9, flags 0/1. Force components must fit motion bounds. No
 * allocation/mutable globals. Output unchanged on failure.
 */
enum mh_error_code	mh_force_prepare(const struct mh_car *car,
	const struct mh_motion_state *motion,
	const struct mh_force_input *input, struct mh_force_result *output,
	struct mh_error *error_out);

#endif

#ifndef MH_MOTION_H
#define MH_MOTION_H

#include "engine/core/error.h"
#include "engine/simulation/suspension.h"

#define MH_MOTION_PASSES 3

struct mh_motion_state {
	/*
	 * Internal physics origin, original world units. Between vehicle ticks,
	 * retains the unrounded inverse of the original stored display pose.
	 */
	double	position[3];

	/* Column-major proper local->world rotation. */
	double	rotation[9];

	/* Body-local, not world velocity. */
	double	velocity[3];

	/* Body-local radians per time unit. */
	double	angular_velocity[3];

	/*
	 * Saved at tick ENTRY, before correction/integration, in that body's
	 * basis. Do not replace these with the previous tick's final velocity.
	 */
	double	previous_velocity[3];
	double	previous_angular_velocity[3];

	/* FL, FR, BR, BL; carried between all three passes. */
	double	extension[4];
};

struct mh_motion_forces {
	/*
	 * Effective local forces, held constant over this tick's three passes.
	 * No implicit engine, tire, gravity, drag or material-force producer.
	 */
	double	traction[4][3];
	double	applied[4][3];

	/* Extra accumulator seed, not applied at a wheel. */
	double	body_force[3];
	double	body_torque[3];
};

struct mh_motion_result {
	struct mh_motion_state	state;

	/*
	 * Each pass's suspension evaluated BEFORE its motion update. These
	 * contacts need not describe the final pose. Totals exclude body seeds.
	 */
	struct mh_suspension	passes[MH_MOTION_PASSES];
};

/*
 * Native reset at a caller-selected INTERNAL pose; zero velocity/history,
 * unit extensions. Use engine/simulation/placement.h for race-grid and route
 * recovery poses. All fallible APIs leave output unchanged on failure.
 */
enum mh_error_code	mh_motion_reset(const double position[3],
	const double rotation[9], struct mh_motion_state *output,
	struct mh_error *error_out);

/*
 * Correction + three force/integration passes, ordinary inertia
 * (3500,2000,1500). The first two passes query collision surfaces; the last
 * reuses the preceding plane for supported wheels, without a miss retry.
 * Contact reuse ends at this call boundary. Local translations retain binary32
 * pose/distance reads and per-axis position stores; rotation, velocity and
 * history correction also retain original stores.
 *
 * This standalone API normalizes its output; vehicle/field stepping defers
 * normalization until after all contacts and display-origin restoration. Use
 * mh_grip_step for material adjustment and damping, or mh_vehicle_step for
 * engine/force/grip feedback and track body collisions.
 *
 * Native bounds: 0 < dt <= .1, |position| <= 1e6, |velocity/history| <= 1e4,
 * |angular velocity/history| <= 1e3, |force/torque inputs| <= 1e9. Input may be
 * &output->state. No allocation or mutable globals; SDL-free.
 */
enum mh_error_code	mh_motion_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *input,
	const struct mh_motion_forces *forces, double step_seconds,
	struct mh_motion_result *output, struct mh_error *error_out);

/*
 * Render boundary: add binary32(.32) along local Y and transform local velocity
 * to world. Separate output arrays.
 */
enum mh_error_code	mh_motion_display(const struct mh_motion_state *state,
	double position[3], double world_velocity[3],
	struct mh_error *error_out);

#endif

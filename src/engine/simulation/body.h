#ifndef MH_BODY_H
#define MH_BODY_H

#include "engine/assets/object_body.h"
#include "engine/core/error.h"
#include "engine/simulation/body_points.h"
#include "engine/simulation/motion.h"

#define MH_BODY_MAX_POINTS 8
#define MH_BODY_IMPACT_PASSES 3

struct mh_body_feedback {
	/* Current frame in driving, one standalone step otherwise. */
	int		contact;

	/* Contact AND latest slice-entry local up Y < -.5. */
	int		strong_air_damping;

	/* Last impact material, or first residual hit. */
	unsigned long	material;
};

struct mh_body_impact {
	struct mh_contact	contact;

	/* Index in the active body points, including upper car points. */
	unsigned int		corner;
	double			impulse;

	/* Before velocity rebasing. */
	double			velocity_delta[3], angular_delta[3];

	/* World-space penetration correction, before dv*dt. */
	double			correction[3];

	/* Scrape metric inputs, before/after impact rotation. */
	double			normal_x, forward;
};

struct mh_body_result {
	struct mh_motion_state	state;
	struct mh_body_feedback	feedback;

	/* Active 4/7 car or 8 scenery points. */
	unsigned int		point_count;
	unsigned int		impact_count, residual_count;
	struct mh_body_impact	impacts[MH_BODY_IMPACT_PASSES];

	/* Active car/scenery points. */
	struct mh_contact	residual[MH_BODY_MAX_POINTS];

	/* Successful casts retained across impact passes. */
	struct mh_contact	contacts[MH_BODY_MAX_POINTS];

	/* Residual local velocity AND position addition. */
	double			push[3];
};

/*
 * Car body response: up to three impact passes followed by residual correction.
 * OLD pose remains the tick-entry INTERNAL pose on every query; current is the
 * integrated/damped pose. Requires CAR suspension/body groups. Four corners use
 * CAR X/Z and center-of-mass Y. No car/object collisions or sound/effect side
 * effects. Query endpoints and impact translations use binary32 stores. Cell
 * ordering and intermediate rounding can differ from the legacy implementation.
 * C89, no SDL/allocation/globals. Same state/dt bounds as motion. Failure
 * preserves all output, including when current == &output->state.
 */
enum mh_error_code	mh_body_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	struct mh_body_result *output, struct mh_error *error_out);

/*
 * Scenery branch: eight corners, configured inertia, ordinary impulse
 * components, and unscaled residual push. No car-specific effects/damping. Same
 * atomic state contract; object motion bounds and 0<dt<=1 allow the original
 * whole-frame scenery interval.
 */
enum mh_error_code	mh_body_object_step(const struct mh_object_body *object,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	struct mh_body_result *output, struct mh_error *error_out);

/*
 * Explicit active car points (4 or 7); mh_body_step is a four-point probe.
 * Driving selects from the final suspension support count and runtime role.
 */
enum mh_error_code	mh_body_step_points(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out);

#endif

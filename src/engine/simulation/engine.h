#ifndef MH_ENGINE_H
#define MH_ENGINE_H

#include "engine/core/error.h"
#include "interfaces/catalog/car.h"

struct mh_engine_options {
	/* Runtime transmission selection, not CAR display text. */
	int	manual;

	/* Reduced acceleration in network races. */
	int	reduced_acceleration;

	/* Double acceleration cheat flag. */
	int	double_acceleration;

	/* Role two selects a distinct automatic force curve. */
	int	role_two;

	/* Suppress role-two wheelspin. */
	int	suppress_role_two_spin;
};

struct mh_engine_config {
	unsigned int			gears;

	/* Speed thresholds, not mechanical ratios. */
	double				ratio[MH_CAR_MAX_GEARS + 1];
	double	min_rpm, max_rpm, acceleration, brake_force;
	struct mh_engine_options	options;
};

struct mh_engine_state {
	/* Zero reverse, 1..gears forward. */
	unsigned int	gear;
	double		rpm;

	/* Shift occurs on release, not press. */
	int		up_ready, down_ready;

	/* OLD value controls wheelspin/engine braking this tick. */
	int		detached;
};

struct mh_engine_input {
	/* Clamped to [0,1], may be suppressed by braking. */
	double	throttle, brake;

	/* Original uses abs(raw steering), before force clamp. */
	double	steering;
	int	shift_up, shift_down, handbrake;
};

struct mh_engine_feedback {
	/* Tick-entry local Z, original world units. */
	double	forward_velocity;

	/* Previous material stage's wheelspin threshold. */
	double	rear_limit;

	/* Either rear wheel supported on the previous final pass. */
	int	rear_supported;

	/* Flag supplied to the original engine call. */
	int	forced_detach;
};

struct mh_engine_result {
	struct mh_engine_state	state;

	/* Effective inputs after clamps/heavy braking. */
	double			throttle, brake;

	/* Engine force E before each rear wheel applies 2E. */
	double			longitudinal;

	/* Braking force consumed by material damping. */
	double			braking;

	/* Speed = abs(local Z * 3.6). */
	double			speed;

	/* Temporary deciding wheelspin eligibility. */
	double			spin_estimate;

	/* Includes pending shifts, unlike the gear used for ratio lookup. */
	unsigned int		candidate_gear;
	int	spin_eligible, wheelspin, heavy_brake, brake_active, engine_braking;
};

/*
 * Engine tuning and reset. All APIs are C89, SDL-free and failure atomic; no
 * allocation or mutable globals. Configuration requires CAR engine and grip
 * groups.
 */
enum mh_error_code	mh_engine_configure(const struct mh_car *car,
	const struct mh_engine_options *options,
	struct mh_engine_config *output, struct mh_error *error_out);
enum mh_error_code	mh_engine_reset(const struct mh_engine_config *config,
	struct mh_engine_state *output, struct mh_error *error_out);

/*
 * Automatic/manual branches, including launch wheelspin, shifts, engine
 * braking, reverse transitions and contact-dependent free revs. Finite native
 * limits and math-domain rejection are documented; this does not clamp away
 * original negative forces or engine overspeed behavior.
 */
enum mh_error_code	mh_engine_step(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input,
	const struct mh_engine_feedback *feedback, double step_seconds,
	struct mh_engine_result *output, struct mh_error *error_out);

#endif

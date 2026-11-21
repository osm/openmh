#ifndef MH_GRIP_H
#define MH_GRIP_H

#include "engine/core/error.h"
#include "engine/simulation/motion.h"
#include "engine/simulation/tyre_effects.h"
#include "interfaces/material/material.h"

#define MH_GRIP_NO_MATERIAL 0xffffffffUL

struct mh_grip_contact {
	int		supported;

	/* Independent of support; zero-initialized original wheels use ID 0. */
	unsigned long	material;

	/* World normal Y, used ONLY on supported wheels. */
	double		normal_y;
};

struct mh_grip_state {
	/* Previous tick's final pass. */
	struct mh_grip_contact	wheels[4];

	/* Phase advanced per wheel with a material ID. */
	double			phase;
	int			sliding;
	double			slide_amount;

	/* Feeds the next engine update's wheelspin test. */
	double			rear_limit;
	struct mh_tyre_effects	tyre_effects;
};

/*
 * Values from the engine and body collision stages. mh_vehicle_step
 * supplies engine outputs and preceding-tick body flags. Gear zero is reverse.
 */
struct mh_grip_control {
	unsigned int	gear;
	double		rpm;

	/* Braking force, already multiplied by PhyBrakeForce. */
	double		braking;

	/* Engine force output. */
	double		longitudinal;
	int		wheelspin, detached, heavy_brake, handbrake;

	/* Rear material contact enables extra longitudinal damping. */
	int		body_contact;

	/* Body contact with entry up Y < -.5. */
	int		strong_air_damping;

	/* Original physics-update flags & 1. */
	int		forced_stop;
};

struct mh_grip_prepared {
	struct mh_motion_forces	forces;

	/* Only phase changes at this stage. */
	struct mh_grip_state	state;

	/* Per-wheel lateral traction multiplier. */
	double			x_scale[4];
};

struct mh_grip_result {
	struct mh_grip_state	state;
	double			velocity[3], angular_velocity[3];
	double			velocity_scale[3], angular_scale[3];

	/* Damped local Z BEFORE the low-speed hold. */
	double			wheel_forward;
	double			brake_factor;
	unsigned int		contacts;
};

struct mh_grip_step_result {
	/* Final state includes material damping. */
	struct mh_motion_result	motion;
	struct mh_grip_prepared	prepared;
	struct mh_grip_result	grip;
};

/*
 * AFTER history correction, BEFORE all three passes. previous_forward is the
 * OLD history value, not corrected->previous_velocity (which has already been
 * replaced with this tick's entry velocity). Applies material steering, bumps,
 * wheelspin shake, weight transfer and roll. Preserves binary32
 * runtime/material reads and phase/force/torque stores. No allocation, mutable
 * globals or SDL. All outputs unchanged on failure.
 */
enum mh_error_code	mh_grip_prepare(const struct mh_materials *materials,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *corrected, double previous_forward,
	const struct mh_motion_forces *forces, double step_seconds,
	struct mh_grip_prepared *output, struct mh_error *error_out);

/*
 * AFTER the three passes. Uses the final pass's supported contacts, not fresh
 * casts at the final pose. Computes slip, velocity damping, low-speed braking
 * and rear-wheel limits. Does not modify velocity history. Publishes rear-tyre
 * effects into state.tyre_effects; callers reset those IDs once per frame,
 * preserving contributions across multiple physics slices. Signed normal Y is
 * retained. Negative lateral friction uses the compatibility power-domain
 * result, zero; zero Y uses reciprocal fallback 10000.
 */
enum mh_error_code	mh_grip_finish(const struct mh_car *car,
	const struct mh_materials *materials, const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion,
	const struct mh_suspension *contacts, double step_seconds,
	struct mh_grip_result *output, struct mh_error *error_out);

/* Same stage with a runtime fractional grip level (0..1000, clamps to 10). */
enum mh_error_code	mh_grip_finish_tuned(const struct mh_car *car,
	const struct mh_materials *materials, const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion,
	const struct mh_suspension *contacts, double grip_level,
	double step_seconds, struct mh_grip_result *output,
	struct mh_error *error_out);

/*
 * Integration entry point. Retains the original ordering around motion's
 * history correction and three passes; caller supplies prepared engine/gravity
 * wheel forces. mh_vehicle_step supplies engine/gearbox feedback and runs
 * track body impacts afterward. Field simulation owns car/object contacts.
 */
enum mh_error_code	mh_grip_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out);
enum mh_error_code	mh_grip_step_tuned(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	double step_seconds, struct mh_grip_step_result *output,
	struct mh_error *error_out);

/* Runtime springs are applied to every suspension pass; NULL is ordinary. */
enum mh_error_code	mh_grip_step_configured(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	const struct mh_suspension_config *suspension, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out);

#endif

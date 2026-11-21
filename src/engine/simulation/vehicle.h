#ifndef MH_VEHICLE_H
#define MH_VEHICLE_H

#include "engine/core/error.h"
#include "engine/simulation/body.h"
#include "engine/simulation/engine.h"
#include "engine/simulation/forces.h"
#include "engine/simulation/grip.h"

struct mh_vehicle_state {
	struct mh_motion_state	motion;
	struct mh_grip_state	grip;
	struct mh_engine_state	engine;

	/* Cached engine-entry km/h, before integration. */
	float			speed;

	/* Current slice: final wheel support and role select 4/7. */
	unsigned int		body_points;

	/* Feeds NEXT tick's grip stage. */
	struct mh_body_feedback	body;
	double			smoothed_steering;

	/* Original front/rear visual angular rates. */
	double			wheel_rate[2];

	/* Final suspension pass, before body integration. */
	struct mh_contact	wheel_contacts[4];

	/* Internal Y saved at tick entry, for mark height. */
	double			contact_origin_y;
	int	brake_active, wheelspin, heavy_brake, engine_braking;
};

struct mh_vehicle_input {
	struct mh_engine_input	controls;

	/* Role: 0 local, 1 remote, 2 AI. */
	unsigned int		role;
	int			reduced_gravity, forced_detach, forced_stop;

	/* Diagnostic overrides, ORed with feedback. */
	int			body_contact, strong_air_damping;
};

struct mh_vehicle_result {
	struct mh_vehicle_state		state;
	struct mh_engine_result		engine;
	struct mh_force_result		forces;

	/*
	 * Raw road/body boundaries, before wrapper restoration/normalization.
	 */
	struct mh_grip_step_result	road;
	struct mh_body_result		body;
};

/*
 * Runtime tuning, separate from immutable CAR data. Fractional grip levels are
 * produced by opponent setup.
 */
struct mh_vehicle_tuning {
	double	turn_force, brake_force, grip_level;

	/* Borrowed for this call; NULL is ordinary. */
	const struct mh_suspension_config	*suspension;
};

/*
 * Native reset at a caller-selected INTERNAL physics pose. Zero motion,
 * empty contact history, gear 1 at idle. See placement.h for grid/recovery
 * poses.
 */
enum mh_error_code	mh_vehicle_reset(const struct mh_engine_config *config,
	const double position[3], const double rotation[9],
	struct mh_vehicle_state *output, struct mh_error *error_out);

/*
 * Handling responsibilities:
 * engine.c     - pedals, wheelspin, drive/braking force, gears and RPM.
 * forces.c     - steering, body loads and per-wheel traction forces.
 * motion.c / suspension.c - integrate forces and resolve wheel contacts.
 * grip.c       - surface force modifiers, friction, braking and sliding.
 * body.c       - track impacts and feedback for the next tick.
 * vehicle.c wires these stages together. motion.c applies grip preparation
 * before integration and grip damping/stopping after the final wheel pass.
 * wheels.c and tyre_effects.c derive presentation state from their results;
 * car/object impacts live in pair.c and object.c outside this update.
 *
 * Tick-entry engine feedback -> forces -> corrected motion/grip -> feedback
 * for the next tick. C89, no SDL, allocation or mutable globals. All output
 * is unchanged on failure, including when input is &output->state.
 * Engine config must have been configured from the supplied car. Use the
 * tuned entry point with matching opponent engine/physics overrides. Native
 * 0 < dt <= .1; original outer update divided time into slices <= .04.
 * Track body impacts run after grip, then the original origin restoration
 * and normalization complete the wrapper. Car/object collisions, race logic and
 * audiovisual side effects remain outside this single-car update.
 */
enum mh_error_code	mh_vehicle_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out);

/* Optional runtime overrides; NULL uses the CAR values, as mh_vehicle_step. */
enum mh_error_code	mh_vehicle_step_tuned(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out);

#endif

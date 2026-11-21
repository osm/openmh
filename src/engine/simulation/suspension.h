#ifndef MH_SUSPENSION_H
#define MH_SUSPENSION_H

#include "engine/core/error.h"
#include "engine/simulation/collision.h"
#include "interfaces/catalog/car.h"

struct mh_suspension_config {
	double	length, strength, damping;
};

struct mh_suspension_input {
	/*
	 * Internal physics origin, after the wrapper's -.32 local-Y shift.
	 * Original world units, no viewer centering/scaling.
	 */
	double	position[3];

	/* Column-major, proper orthonormal local->world basis. */
	double	rotation[9];

	/* FL, FR, BR, BL; reset value is 1. */
	double	previous_extension[4];

	/*
	 * Effective body-local wheel forces supplied by the caller. Traction
	 * is projected only for supported wheels. Applied force is
	 * unconditional. Tire/engine/gravity calculations producing these
	 * inputs are separate.
	 */
	double	traction[4][3];
	double	applied[4][3];
};

struct mh_suspension_wheel {
	/* Raw hit, including rejected t <= .5 hits. */
	struct mh_contact	contact;

	/* Valid suspension contact, stored fraction strictly > .5. */
	int			supported;
	double			start[3];
	double			end[3];
	double			extension;

	/* Contribution along body-local +Y. */
	double			spring_force;

	/* Signed, from extension change; no division by dt. */
	double			damper_force;
	double			traction_scale;

	/* Stored body-local force, including caller's applied force. */
	double			force[3];

	/*
	 * Cross product with the stored local arm; rounded during accumulation.
	 */
	double			torque[3];
};

struct mh_suspension {
	struct mh_suspension_wheel	wheels[4];
	double				force[3];
	double				torque[3];
};

/*
 * Including branches. Demon (mask 1) precedes G-ride (mask 4); other bits do
 * not affect springs. Hidden modes retain the original binary32 stores.
 * Ordinary configuration retains the native CAR parser's double precision.
 * Assets remain immutable. Effective length in (0,1e4], strength in [0,1e9].
 * Failure is atomic.
 */
enum mh_error_code	mh_suspension_configure(const struct mh_car *car,
	unsigned int flags, struct mh_suspension_config *output,
	struct mh_error *error_out);

/*
 * Ordinary flag-clear wheel pass. Evaluates one pose; caller retains returned
 * extensions for the next pass. Damping is the original constant 65000 times
 * the extension difference. Query construction retains the original binary32
 * inputs and intermediate attachment/spring/endpoint stores. Wheel-force
 * arithmetic and per-wheel accumulator stores also retain original order and
 * precision.
 *
 * This public one-pose query always traverses collision surfaces. Motion's
 * internal pass loop separately enables the cached-plane helper. Does not
 * integrate motion or compute tire inputs. No mutable globals. Requires
 * immutable decoded collision data; performs no allocation. Output unchanged on
 * failure.
 *
 * Car coordinates/length <= 1e4, mass <= 1e4, strength <= 1e9; forces <= 1e9
 * per input component; extensions in [0,1]. Pose/derived cast endpoints must
 * fit collision's coordinate limits.
 */
enum mh_error_code	mh_suspension_evaluate(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_suspension_input *input,
	struct mh_suspension *output, struct mh_error *error_out);

/*
 * NULL configuration selects ordinary CAR springs and damping=65000.
 * Negative damping is intentional for G-ride; permitted range +/-1e9.
 */
enum mh_error_code	mh_suspension_evaluate_configured(
	const struct mh_car *car, const struct mh_collision *col,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input, struct mh_suspension *output,
	struct mh_error *error_out);

#endif

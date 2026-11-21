#ifndef MH_WHEELS_H
#define MH_WHEELS_H

#include "engine/core/error.h"

struct mh_wheel_input {
	double		ratio, rpm;

	/* Velocity BEFORE low-speed grip hold. */
	double		forward_velocity;
	unsigned int	gear;
	int	heavy_brake, wheelspin, rear_supported, up_ready, down_ready;
};

struct mh_wheel_state {
	/* Front/rear radians, original twenty-revolution wrap. */
	double		angle[2];

	/* Calculated speed blur models 0..3 for the next frame. */
	unsigned int	variant[2];

	/* This frame binds the prior variants before advancing. */
	unsigned int	drawn[2];
};

/*
 * Original visual rates after material damping, before low-speed braking and
 * body contacts. Preserves binary32 stores. Native bounds: ratio 1..20000, RPM
 * 0..1e6, |velocity|<=1e4, gear<=9, boolean flags. No state, allocation or
 * renderer dependency.
 */
enum mh_error_code	mh_wheel_rates(const struct mh_wheel_input *input,
	double rates[2], struct mh_error *error_out);

/*
 * Integrate and wrap once at +/-125.6637, then select blur variants from
 * absolute angular travel. Native |angle|,|rate|<=1e6, 0<=dt<=1. Outputs are
 * unchanged on error; input/output may alias.
 */
enum mh_error_code	mh_wheel_advance(const struct mh_wheel_state *state,
	const double rates[2], double elapsed_seconds,
	struct mh_wheel_state *output, struct mh_error *error_out);

/*
 * Bind previous variants, then advance only above .0001. Unhalved original
 * frame cap is one second. No physics or camera effects.
 */
enum mh_error_code	mh_wheel_frame(const struct mh_wheel_state *state,
	const double rates[2], float elapsed_seconds,
	struct mh_wheel_state *output, struct mh_error *error_out);

#endif

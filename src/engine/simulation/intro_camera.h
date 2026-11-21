#ifndef MH_INTRO_CAMERA_H
#define MH_INTRO_CAMERA_H

#include "engine/core/error.h"

/*
 * Late-frame opening-camera controller. These flags are independent (inversion
 * can change the effective in-car flag without changing its request). No
 * audio/lens writes.
 */
struct mh_intro_camera_state {
	int	enabled, saved_in_car, in_car, requested_in_car, chase, track;
};

struct mh_intro_camera_input {
	/* Unsigned 32-bit; zero means unreleased. */
	unsigned long	start_ms;

	/* One clock snapshot for this presentation frame. */
	double		now_ms;

	/* Nonempty StartSpline1/2 names. */
	int		has_first_motion, has_second_motion;
};

struct mh_intro_camera_sample {
	/* 0 = StartSpline1, 1 = StartSpline2. */
	unsigned int	motion_index;

	/* Raw LWMO key time, not seconds. */
	float		frame;
};

struct mh_intro_camera_result {
	struct mh_intro_camera_state	state;
	unsigned int			count;

	/* In original call order. */
	struct mh_intro_camera_sample	samples[3];
};

/*
 * Strict time boundaries and unsigned deadline addition are preserved.
 * The original queries its clock separately at each comparison; this API
 * holds that clock constant for a frame. Failure leaves output unchanged.
 */
enum mh_error_code	mh_intro_camera_step(
	const struct mh_intro_camera_state *state,
	const struct mh_intro_camera_input *input,
	struct mh_intro_camera_result *output, struct mh_error *error_out);

#endif

#ifndef MH_FOG_H
#define MH_FOG_H

#include "engine/core/error.h"

/*
 * Native hardware fog. RGB is the clamped, truncated TRK CueColour / 255;
 * inverse_far is binary32(1 / camera far). No platform types or allocation.
 */
struct mh_fog {
	float	color[3];
	float	inverse_far;
};

/*
 * Finite binary32 colors are clamped to 0..255. Camera far must already have
 * the race-camera clamp (60..300). Atomic on error.
 */
enum mh_error_code	mh_fog_configure(const double color[3],
	double far_distance, struct mh_fog *fog, struct mh_error *error_out);

/*
 * Packed visibility alpha, BEFORE Glide's 255-alpha conversion. Input is the
 * projected cache's binary32 camera-Z / far. Returns -1 for nonfinite input.
 * Ordinary opaque material path.
 */
int	mh_fog_visibility(float depth);

/*
 * Background contribution of fully distant faces, byte 0..255. This is the
 * separate .8..( .8+1/7 ) fade, not the ordinary fog ramp. Returns -1 for
 * nonfinite input.
 */
int	mh_fog_background(float depth);

/* Same ramp with the start rounded to binary32 before subtraction. */
int	mh_fog_background_f32(float depth);

/*
 * All original corners must exceed binary64 .7. Returns -1 for invalid input;
 * count must be three or four.
 */
int	mh_fog_distant(const float *depths, unsigned int count);

/*
 * 200 far-to-near buckets. near_mask excludes corners behind the near plane
 * from the sum, retaining the original corner divisor. Returns -1 for invalid
 * inputs or an overflowing depth sum.
 */
int	mh_fog_alpha_bucket(const float *depths, unsigned int count,
	unsigned int near_mask);

#endif

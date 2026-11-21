#ifndef MH_SOFTWARE_FOG_H
#define MH_SOFTWARE_FOG_H

#include "backends/render/software_projection.h"
#include "interfaces/render/renderer.h"

struct mh_software_fog {
	float	near_distance, start_distance, inverse_range;
};

/* Compute fog in scene binary32 precision. Errors preserve output. */
enum mh_error_code	mh_software_fog_setup(float near_distance,
	float far_distance, float start, struct mh_software_fog *output,
	struct mh_error *error_out);

/*
 * Ordinary producer cue before splitting: average source camera depth, with
 * near corners contributing near_distance. All points must have passed far
 * and common-outcode rejection. No upper clamp is added to the original ramp.
 */
enum mh_error_code	mh_software_fog_amount(
	const struct mh_software_fog *fog,
	const struct mh_software_point points[4], unsigned int count,
	float *output, struct mh_error *error_out);

/*
 * Original panorama row averages from tightly packed little-endian RGB555/565.
 * Does not resample the image. Only height and the populated row prefix change;
 * unused table entries, top and edge colors retain their previous values.
 * Start with a zeroed gradient for a fresh session. Errors preserve output.
 */
enum mh_error_code	mh_software_fog_rows(const unsigned char *pixels,
	size_t size, unsigned int width, unsigned int height,
	unsigned int green_bits, struct mh_depth_cue_gradient *gradient,
	struct mh_error *error_out);

/*
 * Original positive-cue color lookup in scene precision. Points have passed
 * common/far rejection; near cached Y is not a screen coordinate and is
 * skipped. Call only for a positive fog amount. Errors preserve color.
 */
enum mh_error_code	mh_software_fog_color(
	const struct mh_depth_cue_gradient *gradient,
	const struct mh_software_point points[4], unsigned int count,
	float color[3], struct mh_error *error_out);

#endif

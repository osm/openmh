#include <float.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/render/fog_internal.h"

#define CAMERA_FAR_MIN 60
#define CAMERA_FAR_MAX 300
#define COLOR_BYTE_MAX 255
#define COLOR_NORMALIZER 255.0
#define VISIBILITY_START .2
#define VISIBILITY_SPAN .5L
#define VISIBILITY_SCALE 2.0L
#define COLOR_BYTE_SCALE 255.0L
#define BACKGROUND_START .8
#define BACKGROUND_SCALE 7.0L
#define DISTANT_START .7
#define ALPHA_BUCKET_LAST 199.0L

enum mh_error_code
mh_fog_configure(const double color[3], double far_distance, struct mh_fog *fog,
	struct mh_error *error_out)
{
	struct mh_fog next;
	float value;
	unsigned int channel;

	if (color == NULL || fog == NULL ||
		!(far_distance >= CAMERA_FAR_MIN &&
			far_distance <= CAMERA_FAR_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hardware fog settings");

	for (channel = 0; channel < 3; channel++) {
		if (!(color[channel] >= -FLT_MAX && color[channel] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid fog color");

		value = (float)color[channel];

		if (value < 0)
			value = 0;

		if (value > COLOR_BYTE_MAX)
			value = COLOR_BYTE_MAX;

		next.color[channel] =
			(float)((unsigned int)value / COLOR_NORMALIZER);
	}

	next.inverse_far = (float)(1.0L / (long double)(float)far_distance);
	*fog = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_fog_visibility(float depth)
{
	double remaining;
	float visibility;

	if (!(depth >= -FLT_MAX && depth <= FLT_MAX))
		return -1;

	if ((double)depth < VISIBILITY_START)
		return COLOR_BYTE_MAX;

	/*
	 * Constants are binary64, evaluated in extended precision before an
	 * explicit binary64 store, then a binary32 store before truncation.
	 */
	remaining = (double)(VISIBILITY_SPAN -
		((long double)depth + (long double)-VISIBILITY_START));

	if (remaining < 0)
		return 0;

	visibility = (float)((long double)remaining * VISIBILITY_SCALE);

	if (visibility > 1)
		visibility = 1;

	return (int)((long double)visibility * COLOR_BYTE_SCALE);
}

int
mh_fog_background_from(float depth, double start)
{
	float value;

	if (!(depth >= -FLT_MAX && depth <= FLT_MAX))
		return -1;

	/*
	 * Inputs beyond the far plane already saturate; avoid overflowing the
	 * later binary32 multiplication for arbitrary finite callers.
	 */
	if (depth >= 1)
		return COLOR_BYTE_MAX;

	value = (float)((long double)depth - start);

	if (value < 0)
		value = 0;

	value = (float)((long double)value * BACKGROUND_SCALE);

	if (value > 1)
		value = 1;

	return (int)((long double)value * COLOR_BYTE_SCALE);
}

int
mh_fog_background(float depth)
{
	return mh_fog_background_from(depth, BACKGROUND_START);
}

int
mh_fog_background_f32(float depth)
{
	/* Store the start as binary32 before subtraction. */
	return mh_fog_background_from(depth, (float)BACKGROUND_START);
}

int
mh_fog_distant(const float *depths, unsigned int count)
{
	unsigned int corner;
	int distant;

	if (depths == NULL || (count != 3 && count != 4))
		return -1;

	distant = 1;

	for (corner = 0; corner < count; corner++) {
		if (!(depths[corner] >= -FLT_MAX && depths[corner] <= FLT_MAX))
			return -1;

		if ((double)depths[corner] <= DISTANT_START)
			distant = 0;
	}

	return distant;
}

int
mh_fog_alpha_bucket(
	const float *depths, unsigned int count, unsigned int near_mask)
{
	unsigned int corner;
	float sum, mean, reverse;
	long double value;

	if (mh_fog_distant(depths, count) < 0 || near_mask >= (1U << count))
		return -1;

	sum = 0;

	for (corner = 0; corner < count; corner++) {
		if (!(near_mask & (1U << corner))) {
			value = (long double)sum + depths[corner];

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return -1;

			sum = (float)value;
		}
	}

	mean = (float)((long double)sum * (float)(1.0 / count));

	if (mean < 0)
		mean = 0;

	if (mean > 1)
		mean = 1;

	reverse = (float)(1.0L - mean);

	return (int)((long double)reverse * ALPHA_BUCKET_LAST);
}

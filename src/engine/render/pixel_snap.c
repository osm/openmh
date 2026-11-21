#include "engine/render/pixel_snap.h"

#define PIXEL_SNAP_BIAS 422212465065984.0L

float
mh_pixel_snap(long double value)
{
	/* Preserve the original extended-precision add/subtract. */
	volatile long double sum;

	sum = value + PIXEL_SNAP_BIAS;

	return (float)(sum - PIXEL_SNAP_BIAS);
}

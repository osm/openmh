#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/pose.h"

#define COMPONENT_LIMIT 1.00002
#define ORTHONORMAL_TOLERANCE 0.00002
#define PITCH_POLE_THRESHOLD 0.00001
#define EXTRAPOLATED_ANGLE_LIMIT 1000000

/* Zero-axis cases return the quadrant angle rounded to float. */
static const double half_pi_float = 1.57079637050628662109375;
static const double three_half_pi_float = 4.7123889923095703125;
static const double two_pi_float = 6.283185482025146484375;
static const double pi = 3.14159265359;
static const double two_pi = 6.28318530718;

static int
matrix_valid(const double r[9])
{
	unsigned int i, j, k;
	double dot, det;

	for (i = 0; i < 9; i++)
		if (!(r[i] >= -COMPONENT_LIMIT && r[i] <= COMPONENT_LIMIT))
			return 0;

	for (i = 0; i < 3; i++)
		for (j = i; j < 3; j++) {
			dot = 0;

			for (k = 0; k < 3; k++)
				dot += r[3 * i + k] * r[3 * j + k];

			if (fabs(dot - (i == j ? 1 : 0)) >
				ORTHONORMAL_TOLERANCE)
				return 0;
		}

	det = r[0] * (r[4] * r[8] - r[5] * r[7]) -
		r[3] * (r[1] * r[8] - r[2] * r[7]) +
		r[6] * (r[1] * r[5] - r[2] * r[4]);

	return fabs(det - 1) <= ORTHONORMAL_TOLERANCE;
}

static void
quaternion(const double r[9], double q[4])
{
	double trace, scale;
	unsigned int i, j, k;

	trace = r[0] + r[4] + r[8];

	if (trace > 0) {
		scale = 2 * sqrt(trace + 1);
		q[3] = .25 * scale;
		q[0] = (r[5] - r[7]) / scale;
		q[1] = (r[6] - r[2]) / scale;
		q[2] = (r[1] - r[3]) / scale;
	} else {
		i = r[0] > r[4] ? 0 : 1;

		if (r[8] > r[4 * i])
			i = 2;

		j = (i + 1) % 3;
		k = (i + 2) % 3;
		scale = 2 * sqrt(1 + r[4 * i] - r[4 * j] - r[4 * k]);
		q[i] = .25 * scale;
		q[j] = (r[3 * i + j] + r[3 * j + i]) / scale;
		q[k] = (r[3 * i + k] + r[3 * k + i]) / scale;
		q[3] = (r[3 * j + k] - r[3 * k + j]) / scale;
	}
}

enum mh_error_code
mh_pose_blend(const double previous[9], const double current[9], double alpha,
	double output[9], struct mh_error *error_out)
{
	double before[4], after[4], q[4], r[9], dot, length;
	unsigned int i;

	if (previous == NULL || current == NULL || output == NULL ||
		!(alpha >= 0 && alpha <= 1) || !matrix_valid(previous) ||
		!matrix_valid(current))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid presentation rotation");

	if (alpha == 0 || alpha == 1) {
		memmove(output, alpha == 0 ? previous : current, sizeof(r));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	quaternion(previous, before);
	quaternion(current, after);
	dot = length = 0;

	for (i = 0; i < 4; i++)
		dot += before[i] * after[i];

	for (i = 0; i < 4; i++) {
		q[i] = (1 - alpha) * before[i] +
			alpha * (dot < 0 ? -after[i] : after[i]);
		length += q[i] * q[i];
	}

	length = sqrt(length);

	for (i = 0; i < 4; i++)
		q[i] /= length;

	r[0] = 1 - 2 * (q[1] * q[1] + q[2] * q[2]);
	r[1] = 2 * (q[0] * q[1] + q[2] * q[3]);
	r[2] = 2 * (q[0] * q[2] - q[1] * q[3]);
	r[3] = 2 * (q[0] * q[1] - q[2] * q[3]);
	r[4] = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
	r[5] = 2 * (q[1] * q[2] + q[0] * q[3]);
	r[6] = 2 * (q[0] * q[2] + q[1] * q[3]);
	r[7] = 2 * (q[1] * q[2] - q[0] * q[3]);
	r[8] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
	memcpy(output, r, sizeof(r));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
angle_xz(double x, double z)
{
	long double result;

	/*
	 * Use atan(z/x) and explicit quadrants, including z==0. Unlike atan2,
	 * +X/zero-Z yields binary32 two-pi, not zero.
	 */
	if (x == 0)
		return z > 0 ? half_pi_float : three_half_pi_float;

	result = atan(z / x);

	if (x < 0)
		result += (long double)pi;
	else if (z <= 0)
		result += (long double)two_pi;

	return (double)(float)result;
}

static double
inverse_cosine(long double x)
{
	/*
	 * Proper rotations stored as floats can exceed the acos domain by a
	 * few ulps.
	 */
	if (x > 1)
		x = 1;

	if (x < -1)
		x = -1;

	return acos((double)x);
}

enum mh_error_code
mh_pose_angles(
	const double rotation[9], double angles[3], struct mh_error *error_out)
{
	double r[9], out[3], x, z, square, inverse;
	long double dot;
	unsigned int i;
	int negative_pitch, negative_roll;

	if (rotation == NULL || angles == NULL || !matrix_valid(rotation))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network rotation");

	for (i = 0; i < 9; i++)
		r[i] = (double)(float)rotation[i];

	x = r[8];
	z = -r[6];
	square = (double)((long double)x * x + (long double)z * z);
	negative_pitch = r[7] > 0;

	if (square < PITCH_POLE_THRESHOLD) {
		out[0] = negative_pitch ? -half_pi_float : half_pi_float;
		out[1] = 0;
		out[2] = angle_xz(r[0], r[2]);

		if (negative_pitch)
			out[0] = (double)(float)(out[0] + two_pi_float);
	} else {
		inverse = 1 / sqrt(square);
		x = (double)(float)((long double)x * inverse);
		z = (double)(float)((long double)z * inverse);
		dot = (long double)x * r[0] + (long double)z * r[2];
		negative_roll = !(r[1] > 0);
		out[2] = (double)(float)(negative_roll ? -inverse_cosine(dot)
						: inverse_cosine(dot));
		dot = (long double)(-z) * r[6] + (long double)x * r[8];
		out[0] = (double)(float)(negative_pitch ? -inverse_cosine(dot)
							: inverse_cosine(dot));
		out[1] = (double)(float)(-angle_xz(x, z) + two_pi_float);

		/* Integer sign-bit comparisons in the original include -0. */
		if (negative_pitch)
			out[0] = (double)(float)(out[0] + two_pi_float);

		if (negative_roll)
			out[2] = (double)(float)(out[2] + two_pi_float);
	}

	memcpy(angles, out, sizeof(out));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_pose_matrix(
	const double angles[3], double rotation[9], struct mh_error *error_out)
{
	long double cx, cy, cz, sx, sy, sz, t;
	double a[3], out[9];
	unsigned int i;

	if (angles == NULL || rotation == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing network angles");

	for (i = 0; i < 3; i++) {
		if (!(angles[i] >= -EXTRAPOLATED_ANGLE_LIMIT &&
			angles[i] <= EXTRAPOLATED_ANGLE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network angle");

		a[i] = (double)(float)angles[i];
	}

	cx = cos(a[0]);
	sx = (float)sin(a[0]);
	cy = cos(a[1]);
	sy = sin(a[1]);
	cz = cos(a[2]);
	sz = sin(a[2]);
	t = sz * sx;
	out[0] = (double)(float)(t * sy + cz * cy);
	out[1] = (double)(float)(sz * cx);
	out[2] = (double)(float)((long double)(float)t * cy - cz * sy);
	t = cz * sx;
	out[3] = (double)(float)(t * sy - sz * cy);
	out[4] = (double)(float)(cz * cx);
	out[5] = (double)(float)((long double)(float)t * cy + sz * sy);
	out[6] = (double)(float)(sy * cx);
	out[7] = (double)(float)(-sx);
	out[8] = (double)(float)(cx * cy);
	memcpy(rotation, out, sizeof(out));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#ifndef MH_POSE_H
#define MH_POSE_H

#include "engine/core/error.h"

/*
 * Convert between column-major local-to-world rotations and Euler radians.
 * Capture normalizes angles to [0, float(2*pi)] and clamps acos inputs
 * for float rounding. Reconstruction accepts finite angles up to 1e6 radians
 * in magnitude for extrapolation. Matrix tolerance is 2e-5. The math library
 * can affect the last bit of a float result. Errors preserve output.
 */
enum mh_error_code	mh_pose_angles(const double rotation[9],
	double angles[3], struct mh_error *error_out);
enum mh_error_code	mh_pose_matrix(const double angles[3],
	double rotation[9], struct mh_error *error_out);

/*
 * Native presentation: shortest-arc normalized quaternion blend. Proper
 * rotations only, alpha in [0,1]; alias-safe and atomic. No Euler wrap.
 */
enum mh_error_code	mh_pose_blend(const double previous[9],
	const double current[9], double alpha, double output[9],
	struct mh_error *error_out);

#endif

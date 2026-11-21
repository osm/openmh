#ifndef MH_ROTATION_H
#define MH_ROTATION_H

/*
 * Column-major local-to-world rotations. Callers supply nonnull arrays.
 * Validation requires a proper orthonormal basis within 1e-6, matching the
 * native suspension/motion domain. The float pose codec uses its separate,
 * wider tolerance; this does not replace its conversion policy.
 */
int	mh_rotation_valid(const double rotation[9]);

/*
 * Explicit validation allowance for accumulated, unnormalized rotations.
 * Checks components, column dot products and determinant without changing
 * the matrix. max_error must be in [0,1); reflected/singular bases fail.
 */
int	mh_rotation_valid_with_error(const double rotation[9],
	double max_error);

/* Shared 1e-3 allowance for unnormalized scenery motion and its render pose. */
int	mh_rotation_valid_accumulated(const double rotation[9]);

/* Postmultiply local X/Y/Z rotations in order; angles are radians. */
void	mh_rotation_turn(double rotation[9], const double angles[3]);

/* Normalize X, then Z=X cross Y, then Y=Z cross X. Requires a valid basis. */
void	mh_rotation_normalize(double rotation[9]);

#endif

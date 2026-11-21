#ifndef MH_MOTION_INTERNAL_H
#define MH_MOTION_INTERNAL_H

#include "engine/simulation/motion.h"

/*
 * State validation shared by integration and body-impact stages.
 * Callers validate pointers; rotation helpers live in engine/core/rotation.h.
 */
int	mh_motion_valid(const struct mh_motion_state *state);

/*
 * Thin scenery can exceed the native car angular bound under original pair
 * response. Angular bound is 1e5; unnormalized scenery rotations have a
 * separate 1e-3 validity allowance for accumulated binary32 rounding.
 * Position/linear/history bounds are unchanged. Neither check clamps state.
 */
int	mh_motion_valid_object(const struct mh_motion_state *state);

/*
 * Original local-axis translation helpers, with binary32 pose/distance reads
 * and a position store after each axis. Trusted bounded, nonaliasing inputs.
 * Rotation, velocities, history and suspension extension stay unchanged.
 */
void	mh_motion_translate(struct mh_motion_state *state,
	const double local[3]);

/* Velocity stores and low-speed lateral hold. */
void	mh_motion_accelerate(struct mh_motion_state *state, double mass,
	const double force[3], const double torque[3], double pass_seconds);

/* Original road rotations; trusted finite inputs, each angle within +/-100. */
void	mh_motion_rotate(double rotation[9], const double angles[3]);

/* A second nonaliasing matrix may share the same coefficients, or be NULL. */
void	mh_motion_rotate_pair(double rotation[9], double other[9],
	const double angles[3]);

/* Stored rotation/translation followed by the original velocity rebasing. */
void	mh_motion_apply(struct mh_motion_state *state, const double angle[3],
	const double translation[3]);
void	mh_motion_correct_history(struct mh_motion_state *state,
	double step_seconds);

/*
 * Motion wrapper boundaries. Between slices, position retains the unrounded
 * internal equivalent of the stored display pose. Entry performs its binary32
 * store. Leave restores the display origin BEFORE normalization, then converts
 * back without a store, retaining the next entry's rounding.
 */
void	mh_motion_enter(struct mh_motion_state *state);
void	mh_motion_leave(struct mh_motion_state *state);

/* With binary32 stores; trusted finite matrix, fallback to identity. */
void	mh_motion_normalize(double rotation[9]);

#endif

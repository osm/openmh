#ifndef MH_PAIR_H
#define MH_PAIR_H

#include "engine/assets/object_body.h"
#include "engine/core/error.h"
#include "engine/simulation/motion.h"

struct mh_pair_contact {
	int		hit;

	/*
	 * Source 0: A point in B, 1: B point in A; corner is its point index.
	 */
	unsigned int	source, corner;
	unsigned long	polygon;

	/* B-local coordinates, before correction. */
	double		point[3], normal[3];

	/* Negative distance along the center-to-center direction. */
	double		depth;
};

struct mh_pair_result {
	struct mh_motion_state	state[2];
	struct mh_pair_contact	contact;

	/* Sound uses the closing-speed numerator before mass/inertia. */
	double			impulse, sound_severity;

	/*
	 * World vector; car, remote and scenery branches distribute it below.
	 */
	double			correction[3];

	/* Each participant's local basis. */
	double			velocity_delta[2][3], angular_delta[2][3];
};

/*
 * Simulated-car narrow-phase contacts. Decoded convex hulls; four CAR corners
 * use center_mass Y. Contacts are B's four retained track corner casts (NULL
 * means no track constraints). No dt: response directly changes positions and
 * local velocities. Does not alter orientations, velocity history, wheels or
 * engine state. C89; no allocation/globals; output unchanged on error, supports
 * aliasing.
 */
enum mh_error_code	mh_pair_step(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct mh_car *car_b, const struct mh_collision *hull_b,
	const struct mh_motion_state *b, const struct mh_contact *contacts_b,
	struct mh_pair_result *output, struct mh_error *error_out);

/*
 * Original role-1 B: full positional correction on A, ordinary two-mass impulse
 * on A, and no B pose/velocity writes. B's diagnostic deltas are zero because
 * they are not applied.
 */
enum mh_error_code	mh_pair_step_remote(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct mh_car *car_b, const struct mh_collision *hull_b,
	const struct mh_motion_state *b, const struct mh_contact *contacts_b,
	struct mh_pair_result *output, struct mh_error *error_out);

/*
 * Original scenery-B branch: eight model-bound corners and object inertia;
 * full positional correction on B, no positional correction on A. Physical
 * impulse still changes both participants. contacts_b, if non-NULL, has
 * eight entries. Caller wakes the object when result.contact.hit is set.
 */
enum mh_error_code	mh_pair_step_object(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	const struct mh_object_body *object_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	const struct mh_contact *contacts_b, struct mh_pair_result *output,
	struct mh_error *error_out);

/*
 * Active geometry variants. Counts must be 4 or 7; contacts_b contains
 * count_b entries. Remote B still participates with its retained count.
 */
enum mh_error_code	mh_pair_step_points(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	unsigned int count_a, const struct mh_car *car_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	unsigned int count_b, const struct mh_contact *contacts_b, int remote_b,
	struct mh_pair_result *output, struct mh_error *error_out);
enum mh_error_code	mh_pair_step_object_points(const struct mh_car *car_a,
	const struct mh_collision *hull_a, const struct mh_motion_state *a,
	unsigned int count_a, const struct mh_object_body *object_b,
	const struct mh_collision *hull_b, const struct mh_motion_state *b,
	const struct mh_contact *contacts_b, struct mh_pair_result *output,
	struct mh_error *error_out);

#endif

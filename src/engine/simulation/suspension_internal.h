#ifndef MH_SUSPENSION_INTERNAL_H
#define MH_SUSPENSION_INTERNAL_H

#include "engine/simulation/suspension.h"

/*
 * Update only hit/fraction against a preceding contact's plane. Internal inputs
 * are bounded endpoints and a valid prior query result. Plane/endpoint reads
 * and distance/fraction stores use original binary32. Position, normal and
 * material stay unchanged; a miss also keeps fraction. No polygon tests or
 * retry. polygon_tests reports zero work for this call.
 */
void	mh_suspension_reuse_contact(const double start[3], const double end[3],
	struct mh_contact *contact);

/*
 * A non-NULL previous pass enables plane reuse for its supported wheels.
 * Other wheels perform full queries. Only motion's eligible passes supply
 * previous; ordinary public evaluations always perform full queries.
 */
enum mh_error_code	mh_suspension_evaluate_cached(const struct mh_car *car,
	const struct mh_collision *col,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input,
	const struct mh_suspension *previous, struct mh_suspension *output,
	struct mh_error *error_out);

/*
 * Wheel arithmetic. Trusted validated inputs; contact already selected.
 * Effective traction (including material X scaling) is binary32. Force is
 * stored, torque retains the cross product until accumulation.
 */
void	mh_suspension_apply_contact(const struct mh_car *car,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input, unsigned int wheel_index,
	struct mh_suspension_wheel *wheel);

/* Initialize accumulators with body seeds BEFORE adding all four wheels. */
void	mh_suspension_accumulate(const struct mh_suspension_wheel *wheel,
	double force[3], double torque[3]);

#endif

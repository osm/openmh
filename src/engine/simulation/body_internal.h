#ifndef MH_BODY_INTERNAL_H
#define MH_BODY_INTERNAL_H

#include "engine/core/error.h"
#include "engine/simulation/body.h"

/*
 * Stages for field interleaving. Caller owns rollback and must begin once,
 * run at most three impact passes, then finish once. Entry pose stays fixed.
 */
enum mh_error_code	mh_body_begin(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out);
enum mh_error_code	mh_body_impact_pass(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	double step_seconds, struct mh_body_result *result, int *hit,
	struct mh_error *error_out);
enum mh_error_code	mh_body_finish(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	double step_seconds, struct mh_body_result *result,
	struct mh_error *error_out);

/*
 * Field residual stage leaves orientation normalization until ALL car and
 * scenery contacts finish. The standalone finish also normalizes.
 */
enum mh_error_code	mh_body_residual(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	double step_seconds, struct mh_body_result *result,
	struct mh_error *error_out);

/*
 * Scenery contact sequence with caller-owned frame latch and sleep state.
 * Both output and frame_impact remain unchanged on failure.
 */
enum mh_error_code	mh_body_object_frame(
	const struct mh_object_body *object, const struct mh_collision *col,
	const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	int sleeping, int *frame_impact, struct mh_body_result *output,
	struct mh_error *error_out);

/* Three impact passes and residual response, before wrapper normalization. */
enum mh_error_code	mh_body_contact_step(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_motion_state *current, double step_seconds,
	unsigned int point_count, struct mh_body_result *output,
	struct mh_error *error_out);

#endif

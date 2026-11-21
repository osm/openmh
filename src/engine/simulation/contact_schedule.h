#ifndef MH_CONTACT_SCHEDULE_H
#define MH_CONTACT_SCHEDULE_H

#include "engine/core/error.h"

enum mh_contact_stage {
	MH_CONTACT_TRACK = 1,
	MH_CONTACT_PAIR,
	MH_CONTACT_RESIDUAL
};

typedef enum mh_error_code (*mh_contact_visit)(void *context,
	enum mh_contact_stage stage, unsigned int car, unsigned int peer,
	unsigned int pass, int *hit, struct mh_error *error_out);

/*
 * Ordinary car pass, after road/object integration. Stable roster order; remote
 * cars are peers only. Each local car completes up to three track/peer passes
 * and its residual work before the next car starts. The callback applies
 * changes immediately; caller owns rollback. Pair hits keep the current car's
 * loop running even when its track query misses. Debug-only contact processing
 * is outside this ordinary path.
 */
enum mh_error_code	mh_contact_schedule(unsigned int count,
	unsigned int active_mask, unsigned int remote_mask,
	mh_contact_visit visit, void *context, struct mh_error *error_out);

typedef enum mh_error_code (*mh_object_contact_visit)(void *context,
	enum mh_contact_stage stage, unsigned int pass, int *hit,
	struct mh_error *error_out);

/*
 * Sleeping skips main passes, not retained-hit residuals. A main hit latches
 * for the outer frame, keeping all three passes active even after a miss.
 * frame_impact is unchanged on failure; caller owns callback rollback and
 * resets the latch once per frame.
 */
enum mh_error_code	mh_object_contact_schedule(int sleeping,
	int *frame_impact, mh_object_contact_visit visit, void *context,
	struct mh_error *error_out);

#endif

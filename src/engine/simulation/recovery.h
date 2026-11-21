#ifndef MH_RECOVERY_H
#define MH_RECOVERY_H

#include "engine/core/error.h"

enum mh_recovery_reason {
	MH_RECOVERY_NONE,
	MH_RECOVERY_FALL,
	MH_RECOVERY_ROOF,
	MH_RECOVERY_SUPPORT,
	MH_RECOVERY_AI
};

struct mh_recovery_state {
	float	roof_seconds, unsupported_seconds;

	/* Reversal timer, retained while the latch is clear. */
	float	ai_seconds;

	/* Latch armed by reversal, cleared by speed or distant recovery. */
	int	ai_latched;
};

struct mh_recovery_feedback {
	/* FL FR BR BL, preceding final suspension pass. */
	int	supported[4];

	/* Body contact with up Y < -.5. */
	int	roof_contact;

	/* Display car pose and shared world-camera eye. */
	float	position[3], camera[3];

	/* Local Z velocity, not speedometer units. */
	float	forward_speed;

	/* Boolean original role-two gate. */
	int	ai_controlled, reversing;
	int	racing, finished;
};

struct mh_recovery_result {
	struct mh_recovery_state	state;
	enum mh_recovery_reason		reason;
};

/*
 * Recovery predicate and caller's fall/start/finish gates. dt is capped at .5.
 * Three or four supports clear both timers, except that the roof timeout is
 * tested first. Exactly two on one side HOLD the support timer; all other pairs
 * advance it. AI reversal arms a retained timer; after >8 seconds and distance
 * >150 from the shared camera, request relocation. Binary32 stores and the
 * pre-store AI timer comparison are preserved. C89, no allocation/globals;
 * output unchanged on failure. A relocation must clear state.
 */
enum mh_error_code	mh_recovery_step(const struct mh_recovery_state *state,
	const struct mh_recovery_feedback *feedback, double dt,
	struct mh_recovery_result *output, struct mh_error *error_out);

/*
 * Predicate/start/finish gates without the preceding display-height branch.
 * After a fall reset, the original still tests the newly placed car once.
 */
enum mh_error_code	mh_recovery_timeout(
	const struct mh_recovery_state *state,
	const struct mh_recovery_feedback *feedback, double dt,
	struct mh_recovery_result *output, struct mh_error *error_out);

#endif

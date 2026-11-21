#ifndef MH_SIM_OBJECT_H
#define MH_SIM_OBJECT_H

#include "engine/core/error.h"
#include "engine/simulation/body.h"

struct mh_object_state {
	/* Scene origin; no car display offset. */
	struct mh_motion_state	motion;

	/* Previous tick's retained impact casts. */
	struct mh_contact	contacts[MH_OBJECT_BODY_POINTS];
	double			sleep_time;
	unsigned int		flags;

	/* Main track hit retained for this frame. */
	int			frame_impact;
};

/* Clear the frame latch, retaining motion/sleep/contacts. */
void	mh_object_begin_frame(struct mh_object_state *state);

/*
 * Strict 150-unit distance from the shared camera to the car's display origin.
 * Binary32 coordinate differences, extended length. The caller retains
 * selection for the frame. Output unchanged on error.
 */
enum mh_error_code	mh_object_car_near(const float camera[3],
	const float position[3], int *selected, struct mh_error *error_out);

/*
 * Free motion: gravity, contact-dependent damping, translation, local-axis
 * rotation and velocity rebasing. Sleeping states do not move. Timer sleep
 * continues this call; falling below Y=-100 stops immediately. Double
 * pose/velocity, binary32 timer/constants. Same motion bounds except angular
 * velocity/history permit +/-1e5 for thin props; 0<=dt<=1. Zero dt still runs
 * sleep/cutoff checks, as in later world calls. Atomic and alias-safe, no
 * allocation/platform dependencies.
 */
enum mh_error_code	mh_object_integrate(const struct mh_object_state *input,
	double step_seconds, struct mh_object_state *output,
	struct mh_error *error_out);

/*
 * Same shared world gravity selector as cars. reduced_gravity is 0/1; ordinary
 * wrapper above passes zero. Sleeping/invalid behavior is unchanged.
 */
enum mh_error_code	mh_object_integrate_gravity(
	const struct mh_object_state *input, double step_seconds,
	int reduced_gravity, struct mh_object_state *output,
	struct mh_error *error_out);

/*
 * The caller saves entry before integration/car contacts and clears sleep bit 2
 * on a car hit. Sleeping skips main impacts; a frame-retained impact still runs
 * residual correction. With positive dt, new impact contacts replace the old
 * array even for sleeping objects. Reset frame_impact with
 * mh_object_begin_frame once before the first slice of a new frame. A zero dt
 * skips all ground response and preserves the old contacts.
 */
enum mh_error_code	mh_object_ground(const struct mh_object_body *body,
	const struct mh_collision *col, const struct mh_motion_state *entry,
	const struct mh_object_state *current, double step_seconds,
	struct mh_object_state *output, struct mh_error *error_out);

#endif

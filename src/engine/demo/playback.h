#ifndef MH_DEMO_PLAYBACK_H
#define MH_DEMO_PLAYBACK_H

#include "engine/core/error.h"
#include "engine/demo/sample.h"

#define MH_DEMO_BUFFER_FRAMES 64
#define MH_DEMO_BENCHMARK_STEP_MS 30UL

struct mh_demo_playback {
	const struct mh_demo	*demo;

	/*
	 * File-frame indices in the original 64-slot ring; next unread file
	 * frame.
	 */
	size_t	frame_indices[MH_DEMO_BUFFER_FRAMES], next_frame;
	unsigned long		anchor_ms, target_ms, now_ms;

	/*
	 * Displayed recorded players; at most seven (2.5) or twelve (3.0) in
	 * Ghost mode.
	 */
	unsigned int		sample_count;
	int			ghost, active, eof;

	/* Fixed target increments; the supplied clock still anchors startup. */
	int			benchmark;

	/*
	 * Refill selector; switches on half crossings until EOF is set.
	 */
	int			active_half;

	/*
	 * Indexed by original recording slot, including holes. A ghost adapter
	 * maps the first seven/twelve present slots after the live human.
	 */
	struct mh_demo_sample	samples[MH_DEMO_MAX_PLAYERS];
};

struct mh_demo_playback_event {
	int		started, ended, updated;
	unsigned int	ring_index; /* 64 if no pair was applied. */

	/* Meaningful only when updated. */
	size_t		older_frame, newer_frame;
};

/*
 * Start with original frame-zero poses and zero derivatives. Borrow an
 * immutable mh_demo and its file bytes for the whole playback. Native playback
 * also accepts the sparse slots emitted by network recording. Reject
 * inconsistent rosters, nonincreasing timestamps and invalid body positions
 * before exposing poses. All disk frames retain their original stride; Ghost
 * mode reserves one version-specific slot for the human, with unavailable
 * forced on. No allocation, OS clock, physics or scene mutation. Failure is
 * atomic.
 */
enum mh_error_code	mh_demo_playback_begin(const struct mh_demo *demo,
	int ghost, struct mh_demo_playback *output, struct mh_error *error_out);

/*
 * Supplied clock is the pause-aware session clock, monotonic u32 milliseconds.
 * The first step establishes the original anchor. A zero anchor is retried on
 * later steps. 64-frame selection and 32-frame refills preserve partial-tail
 * old slots and exact-boundary EOF behavior. End retains the last applied pose;
 * later calls produce no update/event. Both state and event remain unchanged on
 * failure.
 */
enum mh_error_code	mh_demo_playback_step(struct mh_demo_playback *playback,
	unsigned long clock_ms, struct mh_demo_playback_event *event,
	struct mh_error *error_out);

#endif

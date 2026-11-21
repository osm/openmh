#ifndef MH_DEMO_RACE_H
#define MH_DEMO_RACE_H

#include "engine/core/error.h"
#include "engine/demo/benchmark.h"
#include "engine/demo/progress.h"
#include "engine/simulation/field.h"

/* Sentinel cannot alias any of the thirteen on-disk slots. */
#define MH_DEMO_HUMAN_SLOT MH_DEMO_MAX_PLAYERS

enum mh_demo_notice {
	MH_DEMO_NOTICE_NONE,
	MH_DEMO_NOTICE_STARTED,
	MH_DEMO_NOTICE_ENDED
};

struct mh_demo_layout {
	unsigned int	count;

	/*
	 * Field entry -> recording slot; MH_DEMO_HUMAN_SLOT marks the live
	 * human.
	 */
	unsigned int	recording[MH_RACE_MAX_CARS];
};

struct mh_demo_race {
	struct mh_field			field;
	struct mh_demo_playback		playback;
	struct mh_demo_benchmark	benchmark;
	struct mh_demo_layout		layout;

	/* Original saved-time cache, indexed by live field entry. */
	unsigned long			best_recorded_ms[MH_RACE_MAX_CARS];

	/* Physics slices, not a clock. */
	unsigned long			ticks;
	unsigned long			notice_ms;
	enum mh_demo_notice		notice;
};

/*
 * Native field entry zero owns local presentation. A demo maps its recorded
 * local slot there, then other recording slots in ascending order. Ghosts
 * follow a separate live human at zero, with at most seven recorded entrants
 * in 2.5 or twelve in 3.0. All frame bodies retain their original stride.
 */
enum mh_error_code	mh_demo_race_layout(const struct mh_demo *demo,
	int ghost, struct mh_demo_layout *output, struct mh_error *error_out);

/*
 * Freshly initialized cars in layout order, shared immutable track assets.
 * Track lap count must match the recording. Frame-zero poses replace grid
 * placement before progress initialization. Caller attaches cameras/scenery.
 * Hulls are a contiguous borrowed array, also used by restart.
 */
enum mh_error_code	mh_demo_race_begin(const struct mh_demo *demo,
	int ghost, const struct mh_drive *cars, unsigned int count,
	const struct mh_collision *hulls, struct mh_demo_race *output,
	struct mh_error *error_out);

/*
 * Original sampled frame clock and car subdivisions, unrounded >6s gate.
 * Playback poses apply once after physics, before wheels/cameras/progress;
 * saved times correct progress before standings. Pause freezes the clock.
 * Race/playback time is uncapped; physical time follows the original cap.
 * All state publishes atomically. Ghost controls apply only to the human.
 */
enum mh_error_code	mh_demo_race_frame(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out);

/*
 * Renew playback/grid and intro anchor while retaining lifetime time,
 * diagnostic slice counts and any staged current-frame clock sample.
 */
enum mh_error_code	mh_demo_race_restart(struct mh_demo_race *race,
	struct mh_error *error_out);

#endif

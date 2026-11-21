#ifndef MH_RACE_H
#define MH_RACE_H

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/route_projection.h"

#define MH_RACE_MAX_GROUPS 6250
#define MH_RACE_SPLIT_HOLD_LIMIT_MS 10000UL

enum mh_race_split {
	MH_RACE_SPLIT_NONE = 0,
	MH_RACE_SPLIT_LAP = 1,
	MH_RACE_SPLIT_MARKER_ONE = 2,
	MH_RACE_SPLIT_MARKER_TWO = 3,
	MH_RACE_SPLIT_COUNT = 4
};

enum mh_time_notice {
	MH_TIME_NOTICE_NONE = 0,
	MH_TIME_NOTICE_PERSONAL = 1,
	MH_TIME_NOTICE_GLOBAL = 2
};

/*
 * Single-car race progress; parsed, unmodified route is borrowed. No
 * allocation, platform clock, renderer or SDL dependency. Group bytes are
 * embedded so session copies and failed multi-tick frames are atomic.
 */
struct mh_race {
	const struct mh_route	*route;

	/* Finish plane; parameter is in raw route-frame units. */
	double			normal[3], plane;
	double			previous_position[3], parameter;

	/* Projection cache; EMPTY after placement/recovery. */
	unsigned long		projection_key;

	/* Raw route channel, updated even after finishing. */
	float			sound_blend;

	/* 16-key coverage groups: total, current and ordered coverage. */
	size_t			groups, group, covered;
	unsigned char		visited[MH_RACE_MAX_GROUPS];
	unsigned int		lap, lap_limit, best_lap;
	int			finished, wrong_way;
	unsigned long	now_ms, lap_start_ms, last_ms, best_ms, backward_ms;

	/*
	 * Held notices survive transient events; lap_ms retains the completed
	 * lap on the crossing tick.
	 */
	unsigned long		lap_ms, split_ms, split_hold_ms;
	unsigned int		split_notice, lap_notice, race_notice;

	/* Record eligibility; recovery clears it until grid setup. */
	unsigned int		record_eligible;

	/* Original 16-byte lap rows; slot zero is the lap-start timestamp. */
	unsigned long		splits[MH_RACE_MAX_LAPS + 2][MH_RACE_SPLIT_COUNT];

	/* mh_race_split; transient events are cleared each step. */
	unsigned int		split_event;

	/* Raised on an accepted lap crossing. */
	int			lap_event;
};

/*
 * Native bounds: 32..100000 route keys; 1..25 laps; markers round to 0..2.
 * Race clock starts immediately at zero (no countdown). Initial display
 * position anchors projection without granting route coverage. Split hold
 * defaults to the original 4000ms option; callers may set it to 0..10000.
 * Finish geometry follows the original binary32 reset producer; clearing
 * times and anchoring immediately remain native initialization policy.
 */
enum mh_error_code	mh_race_start(const struct mh_route *route,
	unsigned int laps, const double position[3], struct mh_race *output,
	struct mh_error *error_out);

/*
 * Absolute monotonic milliseconds, at most 2147483647. Finished times freeze.
 * Input is the car DISPLAY position. All operations preserve output on failure.
 */
enum mh_error_code	mh_race_step(const struct mh_race *race,
	const double position[3], unsigned long now_ms, struct mh_race *output,
	struct mh_error *error_out);

/*
 * Native recovery policy: re-anchor without awarding a split/lap or changing
 * clock/coverage. Clears record eligibility. Grid restart instead calls
 * mh_race_start; the session then assigns eligible entrants.
 */
enum mh_error_code	mh_race_rebase(const struct mh_race *race,
	const double position[3], struct mh_race *output,
	struct mh_error *error_out);

/*
 * Running classification score. Counts nonzero visited bytes (not ordered
 * covered), suppressing <2 flags behind the finish plane. Binary32
 * geometry/parameter operands and extended accumulations preserve score
 * rounding; route projection and plane initialization are separate. Finished
 * score is 0x7fffffff-now_ms. Unsigned-32 bound; atomic output.
 */
enum mh_error_code	mh_race_score(const struct mh_race *race,
	unsigned long *output, struct mh_error *error_out);

#endif

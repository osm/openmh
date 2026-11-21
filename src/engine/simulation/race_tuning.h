#ifndef MH_RACE_TUNING_H
#define MH_RACE_TUNING_H

#include "engine/core/error.h"
#define MH_RACE_TUNING_CARS 8
#define MH_RACE_TUNING_GEARS 6

/*
 * Rank-indexed history. Index zero here means rank one. The curve reads the
 * PREVIOUS pass's progress; car updates then replace each rank's progress
 * without recalculating the current curve.
 */
struct mh_race_tuning {
	float	progress[MH_RACE_TUNING_CARS];
	float	bias[MH_RACE_TUNING_CARS];
	float	local_bias;
};

/*
 * Original binary32 runtime values and saved setup values. Reverse and
 * unused ratios survive application; forward ratios and acceleration are
 * replaced from their bases, never multiplied cumulatively.
 */
struct mh_race_tuning_engine {
	unsigned int	gears;
	float		base_ratio[MH_RACE_TUNING_GEARS + 1], base_acceleration;
	float		ratio[MH_RACE_TUNING_GEARS + 1], acceleration;
};

enum mh_race_tuning_phase {
	MH_TUNING_NONE,
	MH_TUNING_LAUNCH,
	MH_TUNING_CATCHUP
};

struct mh_race_tuning_plan {
	enum mh_race_tuning_phase	phase;
	int				suppress_role_two_spin;
	unsigned int			apply_mask;
	float				weight;
};

/*
 * Count 1..8, local rank 0..8 (zero selects neutral reference). Slots above
 * count retain their values, including a selected stale slot.
 */
enum mh_error_code	mh_race_tuning_curve(struct mh_race_tuning *state,
	unsigned int count, unsigned int local_rank,
	struct mh_error *error_out);

/*
 * Point is the AI point index, not route checkpoint/visited count. Rank is
 * clamped to 1..8. checkpoint_two tests whether the second checkpoint has been
 * crossed; finished adds one whole lap. Bounds: lap 1..25, point<count<=100000,
 * weight in [-1,1].
 */
enum mh_error_code	mh_race_tuning_car(struct mh_race_tuning *state,
	unsigned long rank, unsigned int lap, int finished,
	unsigned long point_count, unsigned long point,
	unsigned long checkpoint_two, float weight,
	struct mh_race_tuning_engine *engine, struct mh_error *error_out);

/*
 * Preserve unsigned rank-1 and ordered/unordered comparisons, including
 * count=1's division-by-zero outcome. No floating-point exception or nonfinite
 * output is required.
 */
enum mh_error_code	mh_race_tuning_launch(unsigned int count,
	unsigned long rank, float weight, struct mh_race_tuning_engine *engine,
	struct mh_error *error_out);

/*
 * Main-loop tuning, with stable supplied clock. Modes 0/1/5 suppress role-two
 * wheelspin. Before GO+7000ms, adjust active nonlocal cars; thereafter only
 * enabled modes 0/1 use catch-up, on ALL active cars. Strength is selected TRK
 * AI[10], scaled by binary32 .01. now_ms retains extended seconds*1000
 * precision. Unsigned GO+7000 wrap is retained. Output is atomic on invalid
 * input.
 */
enum mh_error_code	mh_race_tuning_schedule(unsigned int mode, int enabled,
	unsigned long go_ms, long double now_ms, float strength,
	unsigned int active_mask, unsigned int local_slot,
	struct mh_race_tuning_plan *output, struct mh_error *error_out);

#endif

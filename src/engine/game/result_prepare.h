#ifndef MH_GAME_RESULT_PREPARE_H
#define MH_GAME_RESULT_PREPARE_H

#include "engine/core/error.h"
#include "engine/game/standings.h"

/*
 * European 3.1 result preparation inputs, separate from physical finish
 * state. Route parameters use the authored motion's raw frame units.
 */
struct mh_result_estimate {
	unsigned int	position;
	int		active, finished;
	unsigned long	lap, lap_limit, total_ms, best_ms;
	float		route_parameter;
};

/*
 * On local abort, put the human last and randomly assign prefix rows
 * 1..active_count-1. This deliberately preserves the original literal row
 * indexing, including its unusual behavior with holes or local != 0. Mode 4 and
 * aborted == 0 are no-ops. Only positions and RNG change; native validation and
 * a bounded rejection loop make failures atomic.
 */
enum mh_error_code	mh_result_abort_positions(
	struct mh_result_estimate *cars, unsigned int count,
	unsigned int local_entry, unsigned int mode, int aborted,
	unsigned long *random, struct mh_error *error_out);

/*
 * Abort ranks and synthetic times replace normal finish estimation. Physical
 * completion and lap records remain untouched. Mode 4 skips preparation. Output
 * and RNG are unchanged on failure.
 */
enum mh_error_code	mh_result_abort_times(struct mh_result_estimate *cars,
	unsigned int count, unsigned int local_entry, unsigned int mode,
	unsigned long *random, struct mh_error *error_out);

/*
 * Preserve finishers, estimate unfinished rows in rank order using current laps
 * and route progress. Only total/best times and RNG change. Mode 4 skips
 * preparation. Native validation makes failures atomic.
 */
enum mh_error_code	mh_result_estimate_times(
	struct mh_result_estimate *cars, unsigned int count, unsigned int mode,
	float route_duration, unsigned long *random,
	struct mh_error *error_out);

#endif

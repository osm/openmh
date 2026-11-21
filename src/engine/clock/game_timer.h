#ifndef MH_GAME_TIMER_H
#define MH_GAME_TIMER_H

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/simulation/field.h"

struct mh_clock_source;

/*
 * Owned by a host/session and retained across menus and races. Initialize once
 * before use. source and its callbacks/context must outlive all timer users;
 * the timer owns no backend resources. Fixed-step tools retain explicit time.
 * The host serializes access; no process-global state is implicit.
 */
struct mh_game_timer {
	const struct mh_clock_source	*source;
	struct mh_clock_accumulator	accumulator;
	struct mh_frame_time		last_race_frame;
	int				started;
};

enum mh_error_code	mh_game_timer_init(struct mh_game_timer *timer,
	const struct mh_clock_source *source, struct mh_error *error_out);

/* Original two startup samples; later starts are idempotent. */
enum mh_error_code	mh_game_timer_start(struct mh_game_timer *timer,
	struct mh_error *error_out);

/*
 * Network lobby entry resets seconds, retaining the counter and last race
 * float timestamp. Its loop advances seconds without sampling race dt.
 */
enum mh_error_code	mh_game_timer_reset(struct mh_game_timer *timer,
	struct mh_error *error_out);
enum mh_error_code	mh_game_timer_step(struct mh_game_timer *timer,
	struct mh_error *error_out);

/*
 * Read accumulated milliseconds without sampling the OS counter. During a race,
 * its field owns the advancing clock until leave().
 */
long double mh_game_timer_ms(const struct mh_game_timer *timer);

/*
 * Absolute real-time query, independent of the accumulated clock. Returns -1 on
 * an invalid OS counter/frequency. The word helper truncates and wraps for
 * keyboard/cursor consumers, preserving output on failure.
 */
long double mh_game_timer_real_ms(const struct mh_game_timer *timer);
enum mh_error_code	mh_game_timer_real_word(
	const struct mh_game_timer *timer, unsigned long *milliseconds,
	struct mh_error *error_out);
enum mh_error_code	mh_game_timer_enter(struct mh_game_timer *timer,
	struct mh_field *field, const struct mh_clock_counter *current,
	struct mh_error *error_out);
void			mh_game_timer_leave(struct mh_game_timer *timer,
	const struct mh_field *field, const struct mh_clock_counter *current);

#endif

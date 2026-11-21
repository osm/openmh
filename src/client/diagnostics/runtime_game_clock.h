#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_GAME_CLOCK_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_GAME_CLOCK_H

#include "engine/core/error.h"

struct mh_game_timer;

/* Native process-lifetime owner for diagnostic launchers. */
struct mh_game_timer	*mh_game_clock_timer(void);

/*
 * Shared diagnostic clock. Portable owners use engine/clock/game_timer.h
 * with an explicitly supplied source. Process-owned live clock. Startup runs
 * the original two initialization samples; later starts are idempotent.
 * Capture/fixed-step tools keep their explicit clocks. No SDL or OS types cross
 * this boundary.
 */
enum mh_error_code	mh_game_clock_start(struct mh_error *error_out);

/*
 * Network lobby entry resets seconds, retaining the counter and last race
 * float timestamp. Its loop advances seconds without sampling race dt.
 */
enum mh_error_code	mh_game_clock_reset(struct mh_error *error_out);

/*
 * Read accumulated milliseconds without sampling the OS counter. During a race,
 * its field owns the advancing clock until the race ends.
 */
long double mh_game_clock_ms(void);

/*
 * Absolute real-time query, independent of the accumulated clock. Returns -1 on
 * an invalid OS counter/frequency. The word helper truncates and wraps for
 * keyboard/cursor consumers, preserving output on failure.
 */
long double mh_game_clock_real_ms(void);
enum mh_error_code	mh_game_clock_real_word(unsigned long *milliseconds,
	struct mh_error *error_out);

#endif

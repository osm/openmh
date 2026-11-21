#include "backends/clock/system_provider.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "engine/clock/game_timer.h"
#include "interfaces/clock/backend.h"

/*
 * Compatibility owner for native diagnostic launchers. Selecting the source
 * does not sample it or start the game clock; the application owns its timer.
 */
struct mh_game_timer *
mh_game_clock_timer(void)
{
	static struct mh_game_timer timer;
	static int initialized;

	if (!initialized) {
		(void)mh_game_timer_init(
			&timer, mh_system_clock_source(), NULL);
		initialized = 1;
	}

	return &timer;
}

enum mh_error_code
mh_game_clock_start(struct mh_error *error_out)
{
	return mh_game_timer_start(mh_game_clock_timer(), error_out);
}

enum mh_error_code
mh_game_clock_reset(struct mh_error *error_out)
{
	return mh_game_timer_reset(mh_game_clock_timer(), error_out);
}

long double
mh_game_clock_ms(void)
{
	return mh_game_timer_ms(mh_game_clock_timer());
}

long double
mh_game_clock_real_ms(void)
{
	return mh_game_timer_real_ms(mh_game_clock_timer());
}

enum mh_error_code
mh_game_clock_real_word(unsigned long *milliseconds, struct mh_error *error_out)
{
	return mh_game_timer_real_word(
		mh_game_clock_timer(), milliseconds, error_out);
}

#include <string.h>

#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "interfaces/clock/clock.h"

enum mh_error_code
mh_game_timer_init(struct mh_game_timer *timer,
	const struct mh_clock_source *source, struct mh_error *error_out)
{
	if (timer == NULL || source == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid game timer source");

	memset(timer, 0, sizeof(*timer));
	timer->source = source;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
reset_counter(struct mh_game_timer *timer, struct mh_error *error_out)
{
	struct mh_clock_counter current, frequency;

	frequency = mh_clock_source_frequency(timer->source);
	current = mh_clock_source_counter(timer->source);

	return mh_clock_accumulator_reset(
		&timer->accumulator, &current, &frequency, error_out);
}

enum mh_error_code
mh_game_timer_start(struct mh_game_timer *timer, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (timer->started)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Reset twice: the second reset removes the initial absolute OS counter
	 * quotient while retaining elapsed startup time.
	 */
	status = reset_counter(timer, error_out);

	if (status == MH_ERROR_NONE)
		status = reset_counter(timer, error_out);

	if (status == MH_ERROR_NONE)
		timer->started = 1;

	return status;
}

enum mh_error_code
mh_game_timer_reset(struct mh_game_timer *timer, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_game_timer_start(timer, error_out);

	return status == MH_ERROR_NONE ? reset_counter(timer, error_out)
				: status;
}

enum mh_error_code
mh_game_timer_step(struct mh_game_timer *timer, struct mh_error *error_out)
{
	struct mh_clock_counter current;
	enum mh_error_code status;

	status = mh_game_timer_start(timer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	current = mh_clock_source_counter(timer->source);

	return mh_clock_accumulator_step(
		&timer->accumulator, &current, 1, error_out);
}

long double
mh_game_timer_ms(const struct mh_game_timer *timer)
{
	return (long double)timer->accumulator.seconds * 1000.0L;
}

long double
mh_game_timer_real_ms(const struct mh_game_timer *timer)
{
	return mh_clock_source_ms(timer->source);
}

enum mh_error_code
mh_game_timer_real_word(const struct mh_game_timer *timer,
	unsigned long *milliseconds, struct mh_error *error_out)
{
	return mh_clock_source_word(timer->source, milliseconds, error_out);
}

enum mh_error_code
mh_game_timer_enter(struct mh_game_timer *timer, struct mh_field *field,
	const struct mh_clock_counter *current, struct mh_error *error_out)
{
	struct mh_clock_accumulator next;
	enum mh_error_code status;

	if (!timer->started)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"game clock was not initialized");

	next = timer->accumulator;
	status = mh_clock_accumulator_step(&next, current, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_clock_load(field, next.seconds,
			&timer->last_race_frame, error_out);

	if (status == MH_ERROR_NONE)
		timer->accumulator = next;

	return status;
}

void
mh_game_timer_leave(struct mh_game_timer *timer, const struct mh_field *field,
	const struct mh_clock_counter *current)
{
	timer->accumulator.seconds = field->cars[0].frame.seconds;

	timer->accumulator.counter = *current;
	timer->last_race_frame = field->cars[0].frame.time;
}

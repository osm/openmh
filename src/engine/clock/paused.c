#include <string.h>

#include "engine/clock/paused.h"
#include "engine/core/limits.h"

static struct mh_clock_counter
subtract(struct mh_clock_counter a, struct mh_clock_counter b)
{
	struct mh_clock_counter result;

	result.low = (a.low - b.low) & MH_U32_MAX;
	result.high = (a.high - b.high - (a.low < b.low)) & MH_U32_MAX;

	return result;
}

static struct mh_clock_counter
counter(void *context)
{
	struct mh_paused_clock *clock;
	struct mh_clock_counter now;

	clock = context;
	now = clock->paused ? clock->stopped
			: mh_clock_source_counter(clock->original);

	if (now.low > MH_U32_MAX || now.high > MH_U32_MAX)
		return now;

	return subtract(now, clock->excluded);
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_paused_clock *clock;

	clock = context;

	return mh_clock_source_frequency(clock->original);
}

static unsigned long
ticks(void *context)
{
	struct mh_paused_clock *clock;
	unsigned long now;

	clock = context;
	now = clock->paused ? clock->stopped_ticks
			: mh_clock_source_ticks(clock->original);

	return (now - clock->excluded_ticks) & MH_U32_MAX;
}

static void
delay(void *context, unsigned long milliseconds)
{
	struct mh_paused_clock *clock;

	clock = context;
	(void)mh_clock_source_delay(clock->original, milliseconds, NULL);
}

static enum mh_error_code
date(void *context, struct mh_clock_date *output, struct mh_error *error_out)
{
	struct mh_paused_clock *clock;

	clock = context;

	return mh_clock_source_date(clock->original, output, error_out);
}

enum mh_error_code
mh_paused_clock_init(struct mh_paused_clock *clock,
	const struct mh_clock_source *source, struct mh_error *error_out)
{
	if (clock == NULL || source == NULL || source->ops == NULL ||
		source->ops->counter == NULL ||
		source->ops->frequency == NULL || source->ops->ticks == NULL ||
		source == &clock->source)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid pausable clock source");

	memset(clock, 0, sizeof(*clock));
	clock->original = source;
	clock->ops.counter = counter;
	clock->ops.frequency = frequency;
	clock->ops.ticks = ticks;
	clock->ops.delay = source->ops->delay == NULL ? NULL : delay;
	clock->ops.date = source->ops->date == NULL ? NULL : date;

	return mh_clock_source_init(
		&clock->source, &clock->ops, clock, error_out);
}

void
mh_paused_clock_set(struct mh_paused_clock *clock, int paused)
{
	struct mh_clock_counter now, elapsed;
	unsigned long low;

	paused = paused != 0;

	if (paused == clock->paused)
		return;

	now = mh_clock_source_counter(clock->original);

	if (paused) {
		clock->stopped = now;
		clock->stopped_ticks = mh_clock_source_ticks(clock->original);
	} else {
		elapsed = subtract(now, clock->stopped);
		low = (clock->excluded.low + elapsed.low) & MH_U32_MAX;
		clock->excluded.high = (clock->excluded.high + elapsed.high +
					(low < clock->excluded.low)) &
			MH_U32_MAX;
		clock->excluded.low = low;
		clock->excluded_ticks =
			(clock->excluded_ticks +
				mh_clock_source_ticks(clock->original) -
				clock->stopped_ticks) &
			MH_U32_MAX;
	}

	clock->paused = paused;
}

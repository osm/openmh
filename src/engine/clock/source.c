#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

enum mh_error_code
mh_clock_source_init(struct mh_clock_source *source,
	const struct mh_clock_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (source == NULL || ops == NULL || ops->counter == NULL ||
		ops->frequency == NULL || ops->ticks == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid clock source callbacks");

	source->ops = ops;
	source->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

struct mh_clock_counter
mh_clock_source_counter(const struct mh_clock_source *source)
{
	return source->ops->counter(source->context);
}

struct mh_clock_counter
mh_clock_source_frequency(const struct mh_clock_source *source)
{
	return source->ops->frequency(source->context);
}

unsigned long
mh_clock_source_ticks(const struct mh_clock_source *source)
{
	return source->ops->ticks(source->context) & MH_U32_MAX;
}

int
mh_clock_source_can_delay(const struct mh_clock_source *source)
{
	return source->ops->delay != NULL;
}

long double
mh_clock_source_ms(const struct mh_clock_source *source)
{
	struct mh_clock_counter current, frequency;
	long double milliseconds;

	current = mh_clock_source_counter(source);
	frequency = mh_clock_source_frequency(source);

	if (mh_clock_counter_ms(&current, &frequency, &milliseconds, NULL) !=
		MH_ERROR_NONE)
		return -1;

	return milliseconds;
}

enum mh_error_code
mh_clock_source_word(const struct mh_clock_source *source,
	unsigned long *milliseconds, struct mh_error *error_out)
{
	return mh_clock_word(
		mh_clock_source_ms(source), milliseconds, error_out);
}

enum mh_error_code
mh_clock_source_delay(const struct mh_clock_source *source,
	unsigned long milliseconds, struct mh_error *error_out)
{
	if (!mh_clock_source_can_delay(source))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"clock source has no blocking delay");

	source->ops->delay(source->context, milliseconds & MH_U32_MAX);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_clock_source_date(const struct mh_clock_source *source,
	struct mh_clock_date *output, struct mh_error *error_out)
{
	struct mh_clock_date date;
	enum mh_error_code status;

	if (source == NULL || source->ops == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid clock calendar query");

	if (source->ops->date == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"clock source has no local calendar");

	status = source->ops->date(source->context, &date, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (date.year < 1900 || date.month < 1 || date.month > 12 ||
		date.day < 1 || date.day > 31 || date.hour > 23 ||
		date.minute > 59)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid clock calendar date");

	*output = date;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

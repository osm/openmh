#include <time.h>

#include "backends/clock/system_platform.h"
#include "interfaces/clock/backend.h"

enum mh_error_code
mh_system_clock_date(
	void *context, struct mh_clock_date *output, struct mh_error *error_out)
{
	time_t now;
	struct tm *date;

	(void)context;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing local calendar output");

	now = time(NULL);

	if (now == (time_t)-1)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read local calendar time");

	date = localtime(&now);

	if (date == NULL || date->tm_year < 0 || date->tm_mon < 0 ||
		date->tm_mon > 11 || date->tm_mday < 1 || date->tm_mday > 31 ||
		date->tm_hour < 0 || date->tm_hour > 23 || date->tm_min < 0 ||
		date->tm_min > 59)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read local calendar date");

	output->year = (unsigned int)date->tm_year + 1900;
	output->month = (unsigned int)date->tm_mon + 1;
	output->day = (unsigned int)date->tm_mday;
	output->hour = (unsigned int)date->tm_hour;
	output->minute = (unsigned int)date->tm_min;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

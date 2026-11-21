#include "client/application/activity.h"
#include "interfaces/clock/clock.h"

enum mh_error_code
mh_activity_run_with_clock(struct mh_activity *activity,
	const struct mh_clock_source *clock, struct mh_error *error_out)
{
	static char failure[MH_ACTIVITY_ERROR_SIZE];
	enum mh_app_status status;
	unsigned long delay;
	struct mh_error error;

	if (!mh_activity_is_open(activity))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "activity is not open");

	if (clock == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing activity clock");

	if (!mh_clock_source_can_delay(clock))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"activity runner requires blocking clock delay");

	do {
		status = mh_activity_step(activity);
		delay = mh_activity_delay(activity);

		if (delay != 0)
			(void)mh_clock_source_delay(clock, delay, NULL);
	} while (status == MH_APP_RUNNING);

	(void)mh_activity_error(activity, &error);
	mh_error_copy(&error, &error, failure, sizeof(failure));

	mh_activity_close(activity);

	return mh_error_forward(error_out, &error);
}

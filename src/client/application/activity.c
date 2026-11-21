#include <string.h>

#include "client/application/activity.h"
#include "engine/core/error.h"

void
mh_activity_init(struct mh_activity *activity)
{
	memset(activity, 0, sizeof(*activity));
}

int
mh_activity_is_open(const struct mh_activity *activity)
{
	return activity != NULL && activity->ops != NULL;
}

enum mh_error_code
mh_activity_start(struct mh_activity *activity, void *context,
	const struct mh_activity_ops *ops, struct mh_error *error_out)
{
	if (activity == NULL || context == NULL || ops == NULL ||
		ops->step == NULL || ops->close == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid activity callbacks");

	if (mh_activity_is_open(activity))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "activity is already open");

	mh_activity_init(activity);
	activity->context = context;
	activity->ops = ops;
	activity->status = MH_APP_RUNNING;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_app_status
mh_activity_step(struct mh_activity *activity)
{
	struct mh_error error;

	if (activity == NULL)
		return MH_APP_FAILED;

	activity->delay = 0;

	if (!mh_activity_is_open(activity) ||
		activity->status != MH_APP_RUNNING)
		return activity->status;

	error = mh_success;
	activity->status = activity->ops->step(
		activity->context, &activity->delay, &error);

	if (error.code != MH_ERROR_NONE ||
		(activity->status != MH_APP_RUNNING &&
			activity->status != MH_APP_DONE &&
			activity->status != MH_APP_FAILED))
		activity->status = MH_APP_FAILED;

	if (activity->status == MH_APP_FAILED) {
		if (error.code == MH_ERROR_NONE)
			mh_error_set(
				&error, MH_ERROR_FAILED, "activity failed");

		mh_error_copy(&activity->error, &error, activity->error_text,
			sizeof(activity->error_text));
	}

	return activity->status;
}

unsigned long
mh_activity_delay(const struct mh_activity *activity)
{
	return activity == NULL ? 0 : activity->delay;
}

enum mh_error_code
mh_activity_error(
	const struct mh_activity *activity, struct mh_error *error_out)
{
	if (activity == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing activity");

	return mh_error_forward(error_out, &activity->error);
}

void
mh_activity_close(struct mh_activity *activity)
{
	const struct mh_activity_ops *ops;
	void *context;

	if (!mh_activity_is_open(activity))
		return;

	ops = activity->ops;
	context = activity->context;
	activity->ops = NULL;
	activity->context = NULL;
	activity->delay = 0;

	if (activity->status == MH_APP_RUNNING)
		activity->status = MH_APP_DONE;

	ops->close(context);
}

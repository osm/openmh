#include "client/application/application_internal.h"
#include "client/application/multiplayer_internal.h"
#include "client/menus/internal.h"

int
mh_application_suspended(const struct mh_application *app)
{
	return app != NULL && app->suspended;
}

enum mh_error_code
mh_application_set_suspended(
	struct mh_application *app, int suspended, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (app == NULL || (suspended != 0 && suspended != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid application suspension");

	if (app->status != MH_APP_RUNNING && !app->storage_failed)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"application is no longer running");

	if (app->suspended == suspended)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (app->lifecycle.set_suspended == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"host suspension is unavailable");

	status = app->lifecycle.set_suspended(
		app->lifecycle.context, suspended, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (suspended)
		mh_multiplayer_suspend(app);

	mh_paused_clock_set(&app->clock, suspended);
	app->suspended = suspended;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_checkpoint(
	struct mh_application *app, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (app == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing application checkpoint");

	if (app->status != MH_APP_RUNNING || !app->storage_prepared ||
		app->storage_waiting)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"application storage is not ready for checkpoint");

	/* A live session owns its progress; never invent a mid-race save. */
	if (app->menus != NULL && app->menus->launch == NULL) {
		status = mh_application_save_menus(app, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_application_begin_storage(app, MH_STORAGE_COMMIT);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_menu_restrict(struct mh_application *app,
	enum mh_menu_feature feature, enum mh_menu_availability state,
	struct mh_error *error_out)
{
	if (app == NULL || (unsigned int)feature >= MH_MENU_FEATURE_COUNT ||
		(unsigned int)state > MH_MENU_HIDDEN)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu restriction");

	if (app->phase != APPLICATION_START || app->menus != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"menus are already starting");

	if (app->menu_configuration.items[feature] < state)
		app->menu_configuration.items[feature] = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

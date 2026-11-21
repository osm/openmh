#include <stdio.h>

#include "backends/audio/openal_provider.h"
#include "backends/render/webgl_provider.h"
#include "backends/runtime/emscripten_provider.h"
#include "backends/storage/indexeddb_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/application/application.h"
#include "client/application/multiplayer.h"
#include "client/hosts/browser_race_api.h"
#include "client/hosts/browser_services.h"
#include "engine/storage/storage.h"

/* Browser notifications are implemented by browser_bridge.js. */
void	mh_browser_finished(int result);
void	mh_browser_storage_failed(const char *message);
int	mh_browser_retry_storage(void);
int	mh_browser_set_suspended(int suspended);
int	mh_browser_checkpoint(void);
int	mh_browser_save_pause(int paused);
void	mh_browser_close(void);

static void	step(void);

static struct mh_application *application;
static struct mh_browser_services host;
static double next_step;
static int storage_failed;
static int checkpoint_failed;
static int save_paused;
static int suspension_requested;

static int
apply_suspension(int suspended)
{
	struct mh_error error;

	if (application == NULL)
		return 1;

	if (mh_application_set_suspended(application, suspended, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "Browser suspension: %s\n", error.message);
		return 0;
	}

	next_step = 0;

	return 1;
}

int
mh_browser_set_suspended(int suspended)
{
	suspension_requested = suspended;

	return apply_suspension(save_paused || suspended);
}

/* Save ownership is independent of user, visibility and graphics suspension. */
int
mh_browser_save_pause(int paused)
{
	if (paused && mh_application_network_active(application))
		return 0;

	if (paused && !apply_suspension(1))
		return 0;

	save_paused = paused != 0;

	return paused ? 1 : apply_suspension(suspension_requested);
}

static void
checkpoint_step(void)
{
	int paused;

	/*
	 * A suspended application may drain storage without advancing game
	 * state.
	 */
	paused = save_paused;
	save_paused = 0;
	step();
	save_paused = paused;
}

int
mh_browser_retry_storage(void)
{
	if (!checkpoint_failed &&
		mh_application_retry_storage(application, NULL) !=
			MH_ERROR_NONE)
		return 0;

	checkpoint_failed = 0;
	storage_failed = 0;
	next_step = 0;

	return 1;
}

/* 1: commit started, 0: busy, -1: explicit retry required. */
int
mh_browser_checkpoint(void)
{
	struct mh_error error;
	enum mh_error_code status;

	if (application == NULL)
		return 1;

	if (storage_failed)
		return -1;

	status = mh_application_checkpoint(application, &error);

	if (status == MH_ERROR_STATE) {
		/*
		 * Hidden pages may stop RAF; finish earlier storage work only.
		 */
		if (mh_application_suspended(application)) {
			next_step = 0;
			checkpoint_step();
		}

		return storage_failed ? -1 : 0;
	}

	if (status != MH_ERROR_NONE) {
		checkpoint_failed = storage_failed = 1;
		mh_browser_storage_failed(error.message);
		return -1;
	}

	/*
	 * The checkpoint owns the next step: start its commit before yielding.
	 */
	next_step = 0;
	checkpoint_step();

	return storage_failed ? -1 : 1;
}

static int
close_application(void)
{
	int result;

	mh_browser_loop_cancel();
	mh_browser_race_detach();
	result = mh_application_close(application);
	application = NULL;
	mh_browser_services_close(&host);
	mh_platform_shutdown();

	return result;
}

static void
step(void)
{
	int result;
	struct mh_error error;
	enum mh_app_status status;

	if (save_paused)
		return;

	if (mh_webgl_presentation_lost()) {
		if (!mh_browser_set_suspended(1))
			return;
	}

	/* RAF can keep running while the browser delays interval callbacks. */
	mh_openal_audio_update(host.audio);

	if (mh_browser_loop_time() < next_step)
		return;

	if (storage_failed)
		return;

	status = mh_application_step(application);
	mh_openal_audio_update(host.audio);

	if (status == MH_APP_RUNNING) {
		next_step = mh_browser_loop_time() +
			(double)mh_application_delay(application);
		return;
	}

	if (mh_application_storage_error(application, &error) !=
		MH_ERROR_NONE) {
		storage_failed = 1;
		mh_browser_storage_failed(error.message);
		return;
	}

	result = close_application();
	mh_browser_finished(result);
}

void
mh_browser_close(void)
{
	if (application == NULL)
		return;

	(void)close_application();
	mh_browser_finished(0);
}

static enum mh_error_code
restrict_menus(struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_application_menu_restrict(application,
		MH_MENU_FEATURE_NETWORK, MH_MENU_DISABLED, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_application_menu_restrict(application,
			MH_MENU_FEATURE_PRESENTATION, MH_MENU_HIDDEN,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_application_menu_restrict(application,
			MH_MENU_FEATURE_SCREENSHOTS, MH_MENU_HIDDEN, error_out);

	return status;
}

static enum mh_error_code
open_application(int argc, char **argv, struct mh_error *error_out)
{
	struct mh_app_services services;
	struct mh_storage *storage;
	enum mh_error_code status;

	status = mh_indexeddb_storage_open(&storage, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_browser_services_open(&host, storage, &services, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_application_open_with_services(
			argc, argv, &services, &application, error_out);
	}

	if (status != MH_ERROR_NONE) {
		mh_browser_services_close(&host);
		mh_storage_close(storage);
		return status;
	}

	status = restrict_menus(error_out);

	if (status != MH_ERROR_NONE) {
		mh_application_close(application);
		application = NULL;
		mh_browser_services_close(&host);
	}

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_error error;

	if (open_application(argc, argv, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "OpenMH: %s\n", error.message);
		mh_browser_finished(1);
		return 1;
	}

	if (!mh_browser_race_attach(application)) {
		fprintf(stderr, "Race API: %s\n", mh_browser_race_error());
		mh_browser_race_detach();
		mh_application_close(application);
		application = NULL;
		mh_browser_services_close(&host);
		mh_browser_finished(1);
		return 1;
	}

	/*
	 * Yield after each step; the engine retains its own fixed tick clock.
	 */
	mh_browser_loop_start(step);

	return 0;
}

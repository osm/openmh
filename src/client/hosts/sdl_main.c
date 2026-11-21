#include <stdio.h>

#ifdef MH_DEBUG
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/session.h"
#endif

#include "backends/window/sdl_provider.h"
#include "client/application/application.h"
#include "client/hosts/clock.h"
#include "client/hosts/sdl_application.h"
#include "engine/core/error.h"
#include "interfaces/clock/clock.h"
#include "interfaces/window/window.h"

static int
run_application(int argc, char *argv[])
{
	struct mh_application *application;
	enum mh_app_status status;
	struct mh_error error;
	unsigned long delay;
	int result;
#ifdef MH_DEBUG
	enum mh_debug_session session;
	char directory[MH_APPLICATION_PATH_SIZE];

	session = mh_debug_session_select(argc, argv);

	if (session != MH_DEBUG_SESSION_NONE) {
		if (mh_filesystem_executable_directory(directory,
			sizeof(directory), &error) != MH_ERROR_NONE) {
			fprintf(stderr, "Game directory: %s\n", error.message);
			return 1;
		}

		result = mh_debug_session_run(session, argc, argv, directory);
		mh_platform_shutdown();

		return result;
	}
#endif

	if (mh_application_open(argc, argv, &application, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "Motorhead: %s\n", error.message);
		return 1;
	}

	do {
		status = mh_application_step(application);
		delay = mh_application_delay(application);

		if (delay != 0)
			mh_clock_delay(delay);
	} while (status == MH_APP_RUNNING);

	result = mh_application_close(application);
	mh_platform_shutdown();

	return result;
}

int
main(int argc, char *argv[])
{
	return mh_sdl_run_application(argc, argv, run_application);
}

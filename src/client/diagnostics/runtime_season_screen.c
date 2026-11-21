#include <stddef.h>

#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_provider.h"
#include "client/diagnostics/runtime_season_screen.h"
#include "client/hosts/clock.h"
#include "interfaces/clock/clock.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/service.h"

#define SEASON_FRAME_DELAY_MS 10

static int
run_screen(const struct mh_results_assets *results,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	const char *footer, const char *capture_path,
	const struct mh_av_settings *settings, int *accepted,
	int *quit_requested, const struct mh_debug_options *debug)
{
	struct mh_season_screen *screen;
	enum mh_app_status status;

	if (mh_season_screen_open(mh_sdl_render_service()->windows, NULL,
		mh_sdl_input_provider(),
		mh_sdl_render_service()->renderers->ui, results, atlas,
		points, footer, capture_path, settings, accepted,
		quit_requested, debug, &screen, NULL) != MH_ERROR_NONE)
		return 0;

	do {
		status = mh_season_screen_step(screen);

		if (status == MH_APP_RUNNING)
			mh_clock_delay(SEASON_FRAME_DELAY_MS);
	} while (status == MH_APP_RUNNING);

	return mh_season_screen_close(screen);
}

int
mh_season_present(const struct mh_results_assets *results,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	const char *footer, const char *capture_path,
	const struct mh_av_settings *settings, int *accepted,
	const struct mh_debug_options *debug)
{
	return run_screen(results, atlas, points, footer, capture_path,
		settings, accepted, NULL, debug);
}

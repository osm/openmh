#include <stdio.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/retro/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/application/opening.h"
#include "client/diagnostics/input.h"
#include "client/diagnostics/media_video.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_network.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/network.h"
#include "client/hosts/sdl_clipboard.h"
#include "client/hosts/sdl_video.h"
#include "interfaces/audio/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/window/backend.h"
#ifdef MH_TEST_NETWORK
#include "client/session/network_game.h"
#include "engine/core/error.h"
#endif

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "capabilities line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const struct mh_content_intro intro = {"movies/movies.pak", 3};
	struct mh_activity activity;
	int outcome;
#ifdef MH_TEST_NETWORK
	struct mh_network_game_request request;
	struct mh_session_options options;
	struct mh_lobby_settings_catalog catalog;
	struct mh_settings settings;
	struct mh_menu_audio *audio;
	unsigned long seed;
	struct mh_error error;
	enum mh_error_code status;
	int quit;
#endif

	CHECK(!mh_capability_available(MH_CAP_NETWORK));
	CHECK(!mh_capability_available(MH_CAP_VIDEO));
	CHECK(!mh_capability_available(MH_CAP_CLIPBOARD));
	CHECK(mh_host_sdl_clipboard() == NULL);
	CHECK(mh_host_network() == NULL);
	CHECK(!mh_capability_available(MH_CAP_SCREENSHOTS));
	CHECK(!mh_capability_available((enum mh_capability) - 1));
	mh_activity_init(&activity);
	CHECK(mh_host_sdl_videos() == NULL);
	CHECK(mh_opening_begin(mh_host_sdl_videos(), mh_filesystem_resources(),
		mh_sdl_window_system(), NULL, "missing-capability-assets",
		1, 0, &intro, &outcome, &activity,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(outcome == MH_OPENING_ENDED &&
		mh_activity_error(&activity, NULL) == MH_ERROR_NONE);
	mh_activity_close(&activity);
#ifdef MH_TEST_NETWORK
	memset(&request, 0, sizeof(request));
	memset(&options, 0, sizeof(options));
	options.canvases = mh_sdl_canvas_provider();
	options.renderers = mh_diagnostic_renderers();
	options.windows = mh_sdl_window_system();
	options.inputs = mh_sdl_input_provider();
	options.audio_provider = mh_sdl_audio_provider();
	options.retro_source = mh_retro_fallback_source();
	options.audio_decoder = mh_audio_fallback_decoder();
	options.audio_trace = 0;
	options.timer = mh_game_clock_timer();
	options.resources = mh_filesystem_resources();
	memset(&catalog, 0, sizeof(catalog));
	memset(&settings, 0, sizeof(settings));
	audio = NULL;
	seed = 17;
	quit = 0;
	status = mh_network_game_open("missing-capability-assets", &request,
		&options, &catalog, &settings, &seed, &audio, &quit, &activity,
		&error);
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message,
			"Network play is unavailable on this platform.") == 0);
	CHECK(!mh_activity_is_open(&activity) && seed == 17 && audio == NULL &&
		quit == 0);
#endif
	puts("Unavailable optional services skip movies and reject network entry before asset or device access");

	return 0;
}

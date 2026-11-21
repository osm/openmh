#include <stdlib.h>

#include "backends/audio/sdl_provider.h"
#include "backends/clock/system_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/video/divi_provider.h"
#include "backends/video/ffmpeg_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/sdl_video.h"
#include "engine/video/playback.h"
#include "interfaces/audio/backend.h"
#include "interfaces/clock/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/capture.h"
#include "interfaces/video/backend.h"

#ifdef MH_HOST_VIDEO
static const struct mh_video_provider *provider;

static void
close_videos(void)
{
	mh_video_playback_close(provider);
	provider = NULL;
}

static enum mh_error_code
save_capture(void *context, const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	(void)context;

	return mh_capture_image(mh_sdl_image_capture(), image, path, error_out);
}
#endif

const struct mh_video_provider *
mh_host_sdl_videos(void)
{
#ifdef MH_HOST_VIDEO
	static struct mh_video_services services;

	if (!mh_capability_available(MH_CAP_VIDEO))
		return NULL;

	/* Native selection is initialized on the host's event thread. */
	if (provider == NULL) {
		services.clips = mh_video_divi_source();
		services.decoder = mh_video_ffmpeg_decoder();
		services.windows = mh_sdl_window_system();
		services.canvases = mh_sdl_canvas_provider();
		services.inputs = mh_sdl_input_provider();
		services.clock = mh_system_clock_source();
		services.audio = mh_sdl_audio_provider();
		services.capture = save_capture;

		if (mh_video_playback_open(&services, &provider, NULL) !=
			MH_ERROR_NONE)
			return NULL;

		/*
		 * This default provider and its services live through process
		 * exit.
		 */
		if (atexit(close_videos) != 0) {
			close_videos();
			return NULL;
		}
	}

	return provider;
#else

	return NULL;
#endif
}

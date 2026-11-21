#include <string.h>

#include "backends/audio/openal_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/webgl_provider.h"
#include "backends/video/divi_provider.h"
#include "backends/video/ffmpeg_provider.h"
#include "client/application/application.h"
#include "client/hosts/browser_render.h"
#include "client/hosts/browser_services.h"
#include "client/hosts/sdl_clipboard.h"
#include "client/hosts/services.h"
#include "interfaces/render/service.h"
#ifdef MH_BROWSER_NETWORK
#include "backends/network/browser_provider.h"
#endif

static enum mh_error_code
set_suspended(void *context, int suspended, struct mh_error *error_out)
{
	struct mh_browser_services *host;
	enum mh_error_code status;

	host = context;

	/*
	 * Do not restart clocks or audio until every presentation owner is
	 * ready.
	 */
	if (!suspended) {
		status = mh_webgl_restore_windows(error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_openal_audio_suspend(host->audio, suspended, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_paused_clock_set(&host->movie_clock, suspended);

	if (!suspended)
		mh_sdl_input_reset();

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_videos(struct mh_browser_services *host,
	const struct mh_app_services *services, struct mh_error *error_out)
{
#ifdef MH_BROWSER_VIDEO
	struct mh_video_services *video;

	video = &host->video_services;
	video->clips = mh_video_divi_source();
	video->decoder = mh_video_ffmpeg_decoder();
	video->windows = services->rendering->windows;
	video->canvases = services->rendering->canvases;
	video->inputs = services->inputs;
	video->clock = &host->movie_clock.source;
	video->audio = host->audio;
	return mh_video_playback_open(video, &host->videos, error_out);
#else
	(void)host;
	(void)services;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
#endif
}

void
mh_browser_services_close(struct mh_browser_services *host)
{
	mh_video_playback_close(host->videos);
	mh_openal_audio_close(host->audio);
	memset(host, 0, sizeof(*host));
}

enum mh_error_code
mh_browser_services_open(struct mh_browser_services *host,
	struct mh_storage *storage, struct mh_app_services *services,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(host, 0, sizeof(*host));
	status = mh_host_common_services(
		services, "/assets", storage, error_out);
	services->rendering = mh_browser_render_service();
	services->inputs = mh_sdl_input_provider();
	services->clipboard = mh_host_sdl_clipboard();

	if (status == MH_ERROR_NONE)
		status = mh_paused_clock_init(
			&host->movie_clock, services->clock, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_openal_audio_open(&host->audio, error_out);

	if (status == MH_ERROR_NONE)
		status = open_videos(host, services, error_out);

	if (status != MH_ERROR_NONE) {
		mh_browser_services_close(host);
		return status;
	}

	host->lifecycle.context = host;
	host->lifecycle.set_suspended = set_suspended;
	services->lifecycle = &host->lifecycle;
	services->audio = host->audio;
	services->videos = host->videos;

	/* Browser transports are independent of native UDP selection. */
#ifdef MH_BROWSER_NETWORK
	services->network = mh_browser_network_provider();
#else
	services->network = NULL;
#endif

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

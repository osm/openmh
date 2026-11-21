#ifndef MH_BROWSER_SERVICES_H
#define MH_BROWSER_SERVICES_H

#include "client/application/host.h"
#include "engine/clock/paused.h"
#include "engine/video/playback.h"

struct mh_app_services;
struct mh_storage;

/* Host owns these services until after application/player/device teardown. */
struct mh_browser_services {
	struct mh_app_lifecycle		lifecycle;
	struct mh_paused_clock		movie_clock;
	struct mh_video_services	video_services;
	const struct mh_video_provider	*videos;
	const struct mh_audio_provider	*audio;
};

enum mh_error_code	mh_browser_services_open(
	struct mh_browser_services *host, struct mh_storage *storage,
	struct mh_app_services *services, struct mh_error *error_out);
void			mh_browser_services_close(
	struct mh_browser_services *host);

#endif

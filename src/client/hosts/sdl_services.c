#include "backends/audio/sdl_provider.h"
#include "backends/input/sdl_provider.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/network.h"
#include "client/hosts/sdl_application.h"
#include "client/hosts/sdl_clipboard.h"
#include "client/hosts/sdl_video.h"
#include "client/hosts/services.h"

enum mh_error_code
mh_host_sdl_services(struct mh_app_services *services, const char *directory,
	struct mh_storage *storage, const struct mh_render_service *rendering,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_host_common_services(
		services, directory, storage, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	services->rendering = rendering;
	services->inputs = mh_sdl_input_provider();
	services->audio = mh_sdl_audio_provider();
	services->videos = mh_host_sdl_videos();
	services->network = mh_host_network();
	services->legacy_network = mh_host_legacy_network();
	services->clipboard = mh_host_sdl_clipboard();
	services->screenshot_data = mh_capability_available(MH_CAP_SCREENSHOTS)
		? services->user_data
		: NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

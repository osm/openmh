#include <stdlib.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/input.h"
#include "client/diagnostics/render_capture.h"
#include "client/diagnostics/runtime_lobby.h"
#include "client/hosts/screenshots.h"
#include "client/hosts/sdl_clipboard.h"
#include "client/hosts/storage.h"
#include "engine/settings/av.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/service.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_network_lobby_open(const char *asset_root, int host,
	struct mh_game_timer *timer, const struct mh_debug_options *debug,
	struct mh_network_lobby **output, struct mh_error *error_out)
{
	return mh_network_lobby_open_from(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_filesystem_resources(),
		asset_root, host, timer, mh_sdl_render_service()->windows, NULL,
		mh_sdl_render_service()->canvases, mh_sdl_input_provider(),
		mh_host_sdl_clipboard(), mh_sdl_render_service()->capture,
		mh_host_user_data(),
		mh_host_screenshot_encoder(mh_screenshot_format_default()),
		debug == NULL ? NULL : debug->screenshot_directory, debug,
		output, error_out);
}

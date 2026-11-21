#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/clock/system_provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/lob_provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/act_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/retro/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "backends/ui/dta_provider.h"
#include "client/application/application.h"
#include "client/hosts/screenshots.h"
#include "client/hosts/services.h"
#include "client/hosts/storage.h"
#include "engine/storage/storage.h"
#include "interfaces/audio/backend.h"
#include "interfaces/clock/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/service.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/user_data_backend.h"
#include "interfaces/window/backend.h"

enum mh_error_code
mh_host_common_services(struct mh_app_services *services, const char *directory,
	struct mh_storage *storage, struct mh_error *error_out)
{
	memset(services, 0, sizeof(*services));

	services->game_directory = directory;

	services->image_source = mh_image_pdi_source();
	services->font_source = mh_font_fallback_source();
	services->wireframe_source = mh_wireframe_lob_source();
	services->sprite_source = mh_sprite_spr_source();

	services->model_decoder = mh_model_myo_decoder();
	services->world_decoder = mh_world_myw_decoder();
	services->atlas_source = mh_atlas_remap_source();
	services->animation_source = mh_animation_lightwave_source();
	services->navigation_source = mh_navigation_mot_dat_source();
	services->catalog_source = mh_catalog_fallback_source();
	services->profile_source = mh_profile_fallback_source();
	services->palette_source = mh_palette_act_source();
	services->layout_source = mh_menu_layout_dta_source();
	services->material_source = mh_material_mat_srf_source();
	services->env_sound_source = mh_env_sounds_esd_source();
	services->horn_source = mh_horn_fallback_source();
	services->collision_source = mh_collision_fallback_source();
	services->image_decoder = mh_image_fallback_decoder();
	services->thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services->portrait_decoder = mh_image_tga_portrait_decoder();
	services->retro_source = mh_retro_fallback_source();
	services->audio_decoder = mh_audio_fallback_decoder();
	services->screenshot_encoders[MH_SCREENSHOT_PNG] =
		mh_host_screenshot_encoder(MH_SCREENSHOT_PNG);
	services->screenshot_encoders[MH_SCREENSHOT_TGA] =
		mh_host_screenshot_encoder(MH_SCREENSHOT_TGA);
	services->user_data = mh_host_user_data();

	if (services->user_data == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"writable storage is unavailable");

	services->resources = mh_filesystem_resources();
	services->resource_archives = mh_zip_resource_provider();
	services->clock = mh_system_clock_source();
	services->storage = storage;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

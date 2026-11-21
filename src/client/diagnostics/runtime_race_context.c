#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/runtime_race_context.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_race_context_load_profile(const char *asset_root, unsigned int car_number,
	unsigned int track_number, int scenery, struct mh_av_settings *settings,
	enum mh_render_profile profile, const unsigned long *texture_budget,
	struct mh_race_context *context, struct mh_error *error_out)
{
	return mh_race_context_load_profile_observed_from(
		mh_horn_fallback_source(), mh_material_mat_srf_source(),
		mh_env_sounds_esd_source(), mh_catalog_fallback_source(),
		mh_navigation_mot_dat_source(), mh_animation_lightwave_source(),
		mh_atlas_remap_source(), mh_model_myo_decoder(),
		mh_world_myw_decoder(), mh_collision_fallback_source(),
		mh_sprite_spr_source(), mh_font_fallback_source(),
		mh_image_pdi_source(), mh_image_fallback_decoder(),
		mh_image_tga_portrait_decoder(), mh_filesystem_resources(),
		asset_root, car_number, track_number, scenery, settings,
		profile, NULL, texture_budget, context, error_out);
}

enum mh_error_code
mh_race_context_load(const char *asset_root, unsigned int car_number,
	unsigned int track_number, int scenery, struct mh_race_context *context,
	struct mh_error *error_out)
{
	return mh_race_context_load_settings(asset_root, car_number,
		track_number, scenery, NULL, context, error_out);
}

enum mh_error_code
mh_race_context_load_settings(const char *asset_root, unsigned int car_number,
	unsigned int track_number, int scenery, struct mh_av_settings *settings,
	struct mh_race_context *context, struct mh_error *error_out)
{
	return mh_race_context_load_profile(asset_root, car_number,
		track_number, scenery, settings, MH_RENDER_GLIDE, NULL, context,
		error_out);
}

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
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_scene.h"
#include "engine/render/scene.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_race_scene_load(const char *root, unsigned int car, unsigned int track,
	int scenery, struct mh_race_scene *scene, struct mh_error *error_out)
{
	return mh_race_scene_load_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_image_pdi_source(),
		mh_image_fallback_decoder(), mh_filesystem_resources(), root,
		car, track, scenery, scene, error_out);
}

enum mh_error_code
mh_race_scene_load_settings(const char *root, unsigned int car,
	unsigned int track, int scenery, const struct mh_av_settings *settings,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	return mh_race_scene_load_settings_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_image_pdi_source(),
		mh_image_fallback_decoder(), mh_filesystem_resources(), root,
		car, track, scenery, settings, scene, error_out);
}

enum mh_error_code
mh_race_scene_names_prepare(const char *root, struct mh_race_scene *scene,
	const struct mh_field *field,
	const struct mh_profile roster[MH_NAME_LABELS_MAX], unsigned int count,
	struct mh_error *error_out)
{
	return mh_race_scene_names_prepare_from(mh_font_fallback_source(),
		mh_filesystem_resources(), root, scene, field, roster, count,
		error_out);
}

enum mh_error_code
mh_race_scene_load_ai(const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	return mh_race_scene_load_ai_from(
		mh_filesystem_resources(), root, scene, error_out);
}

enum mh_error_code
mh_race_scene_add_opponents(const char *root, unsigned int car,
	unsigned int track, unsigned int count, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	return mh_race_scene_add_opponents_from(mh_filesystem_resources(), root,
		car, track, count, scene, error_out);
}

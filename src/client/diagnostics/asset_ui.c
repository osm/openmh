#include "backends/catalog/provider.h"
#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/image/tga_provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_ui.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_hud_assets_load(const char *root, struct mh_hud_assets *output,
	struct mh_error *error_out)
{
	return mh_hud_assets_load_from(mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_filesystem_resources(), root,
		output, error_out);
}

enum mh_error_code
mh_hud_assets_map(const char *root, const struct mh_track *track,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	return mh_hud_assets_map_from(mh_image_fallback_decoder(),
		mh_filesystem_resources(), root, track, hud, error_out);
}

enum mh_error_code
mh_results_assets_load(const char *root, unsigned int count, const char *name,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out)
{
	return mh_results_assets_load_from(mh_profile_fallback_source(),
		mh_font_fallback_source(), mh_image_fallback_decoder(),
		mh_image_tga_portrait_decoder(), mh_filesystem_resources(),
		root, count, name, atlas, output, error_out);
}

enum mh_error_code
mh_results_assets_profiles(const char *root, unsigned int count,
	const struct mh_profile *const *profiles, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out)
{
	return mh_results_assets_profiles_from(mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		mh_filesystem_resources(), root, count, profiles, atlas, output,
		error_out);
}

enum mh_error_code
mh_results_assets_roster(const char *root, unsigned int count,
	const struct mh_profile *const *profiles, unsigned int human_mask,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out)
{
	return mh_results_assets_roster_from(mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		mh_filesystem_resources(), root, count, profiles, human_mask,
		atlas, output, error_out);
}

enum mh_error_code
mh_results_assets_season(const char *root, unsigned int count,
	const struct mh_profile *const *profiles, unsigned int human_mask,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out)
{
	return mh_results_assets_season_from(mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		mh_filesystem_resources(), root, count, profiles, human_mask,
		atlas, output, error_out);
}

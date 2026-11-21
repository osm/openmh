#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_UI_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_UI_H

#include "engine/ui/hud_assets.h"
#include "engine/ui/results_assets.h"

/*
 * Native conveniences select filesystem resources. Ownership and failure
 * guarantees follow the corresponding provider-taking operations.
 */

enum mh_error_code	mh_hud_assets_load(const char *root,
	struct mh_hud_assets *output, struct mh_error *error_out);

enum mh_error_code	mh_hud_assets_map(const char *root,
	const struct mh_track *track, struct mh_hud_assets *hud,
	struct mh_error *error_out);

enum mh_error_code	mh_results_assets_load(const char *root,
	unsigned int count, const char *name, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

enum mh_error_code	mh_results_assets_profiles(const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out);

enum mh_error_code	mh_results_assets_roster(const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

enum mh_error_code	mh_results_assets_season(const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

#endif

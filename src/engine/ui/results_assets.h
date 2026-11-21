#ifndef MH_RESULTS_ASSET_LOADER_H
#define MH_RESULTS_ASSET_LOADER_H

#include "engine/core/error.h"
#include "engine/ui/results.h"
#include "interfaces/catalog/profile.h"

struct mh_profile_source;

struct mh_font_source_provider;

struct mh_resource_source;
struct mh_image_decoder_provider;

/*
 * Providers and resources are borrowed during loading. Driver definitions,
 * background images, portraits and fonts use their selected sources/decoders.
 * Appends to the shared atlas; caller discards the atlas on failure.
 * Missing/unreadable portraits use the original opaque black fallback;
 * truncated readable images and missing background/font assets still fail.
 */
enum mh_error_code	mh_results_assets_load_from(
	const struct mh_profile_source *profile_source,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const char *name, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

/* Native multiplayer identities use the shared original player portrait. */
enum mh_error_code	mh_results_assets_players_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const char *const *names, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

/*
 * Bound league profiles in runtime roster order: local human at zero, AI
 * thereafter. Copies profile metadata and loads each actual AI portrait.
 * Human portrait is Game/player.tga. Discard atlas on asset-load failure;
 * output remains unchanged. Malformed profile fields are rejected before I/O.
 */
enum mh_error_code	mh_results_assets_profiles_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out);

/* Explicit human portrait mask; zero retains all AI portraits for attract. */
enum mh_error_code	mh_results_assets_roster_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

/*
 * Sorted division order, rather than runtime entry order. Human mask selects
 * player portraits at any row; loads original lresults/lresults2 backgrounds.
 */
enum mh_error_code	mh_results_assets_season_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out);

#endif

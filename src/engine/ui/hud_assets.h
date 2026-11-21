#ifndef MH_HUD_ASSET_LOADER_H
#define MH_HUD_ASSET_LOADER_H

#include "engine/core/error.h"
#include "engine/ui/hud.h"
#include "interfaces/catalog/track.h"

struct mh_font_source_provider;
struct mh_sprite_source_provider;

struct mh_resource_source;
struct mh_image_decoder_provider;

/* Original FONT2 byte advances are shared by notification/chat producers. */
enum mh_error_code	mh_hud_assets_message_widths_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	unsigned int advances[MH_FONT_GLYPHS], struct mh_error *error_out);

/*
 * Load into empty storage. Output owns its atlas; free atlas.image.rgba before
 * reloading. Resources and decoder are borrowed during loading; failure
 * preserves output.
 */
enum mh_error_code	mh_hud_assets_load_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *output, struct mh_error *error_out);

/*
 * Build a GPU atlas from a software edition's installed instruments and fonts.
 */
enum mh_error_code	mh_hud_assets_load_software_from(
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_hud_assets *output,
	struct mh_error *error_out);

/* Append the track-specific original map before uploading the shared atlas. */
enum mh_error_code	mh_hud_assets_map_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_hud_assets *hud,
	struct mh_error *error_out);

#endif

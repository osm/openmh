#ifndef MH_LOADING_ASSETS_H
#define MH_LOADING_ASSETS_H

#include "engine/ui/draw.h"
#include "engine/ui/loading_screen.h"

struct mh_resource_source;

struct mh_image_decoder_provider;
struct mh_font_source_provider;

struct mh_loading_assets {
	struct mh_ui_atlas	atlas;
	struct mh_ui_region	background, glyphs[MH_FONT_GLYPHS];
	struct mh_ui_region	indicator;
	unsigned int		advance[MH_FONT_GLYPHS];
	struct mh_font_program	word_font;
	enum mh_render_profile	profile;
};

/*
 * Owns only loading artwork and the selected caption font. Load into empty
 * storage; failure leaves output unchanged. Provider is borrowed during load.
 * The software profile prepares RGB565 artwork at the requested viewport
 * size and keeps FONT2 byte writes, at every resolution. Draw at that size.
 */
enum mh_error_code	mh_loading_assets_load_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	enum mh_render_profile profile, unsigned int width, unsigned int height,
	struct mh_loading_assets *output, struct mh_error *error_out);
void			mh_loading_assets_free(
	struct mh_loading_assets *assets);

/*
 * Append background and captions to an initialized UI list. Failed draws
 * restore its counts. Borrow assets through renderer submission, since
 * software text commands reference word_font. Indicators/stage timing are
 * separate; this composes the initial static screen for either profile.
 */
enum mh_error_code	mh_loading_draw(const struct mh_loading_assets *assets,
	const struct mh_loading_layout *layout, struct mh_ui_list *list,
	struct mh_error *error_out);

/* Native ready prompt, separate from the course captions. */
enum mh_error_code	mh_loading_draw_ready(
	const struct mh_loading_assets *assets, struct mh_ui_list *list,
	struct mh_error *error_out);

/* Append frame-zero markers in both columns; Glide draws no indicators. */
enum mh_error_code	mh_loading_draw_progress(
	const struct mh_loading_assets *assets,
	const struct mh_loading_progress *progress, struct mh_ui_list *list,
	struct mh_error *error_out);

#endif

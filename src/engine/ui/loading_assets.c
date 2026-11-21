#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/path_menus.h"
#include "engine/image/load.h"
#include "engine/render/viewport.h"
#include "engine/ui/font_atlas.h"
#include "engine/ui/loading_art.h"
#include "engine/ui/loading_assets.h"
#include "engine/ui/loading_resize.h"
#include "interfaces/font/source.h"
#include "interfaces/storage/resource.h"

#define ATLAS_WIDTH 1024
#define ATLAS_HEIGHT 512
#define IMAGE_PADDING 2

static enum mh_error_code
load_background(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_loading_assets *assets, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	struct mh_image image, resized;
	enum mh_error_code status;

	status = mh_image_load_from(decoder, resources, root,
		MH_ASSET_LOADING_BACKDROP, NULL, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_loading_background_check(&image, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	if (assets->profile == MH_RENDER_SOFTWARE) {
		status = mh_loading_image_resize(
			&image, width, height, 0, &resized, error_out);
		free(image.rgba);

		if (status != MH_ERROR_NONE)
			return status;

		image = resized;
	}

	status = mh_ui_atlas_add(
		&assets->atlas, &image, &assets->background, error_out);
	free(image.rgba);

	return status;
}

static enum mh_error_code
load_font(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_loading_assets *assets, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_font font;
	enum mh_error_code status;

	if (assets->profile == MH_RENDER_SOFTWARE)
		return mh_font_load_program(fonts, resources, root,
			MH_ASSET_FONT2_LOWERCASE, &assets->word_font,
			error_out);

	status = mh_image_read_from(decoder, resources, root,
		MH_ASSET_HUD_FONT_ATLAS, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_hardware_decode_from(
		decoder, file.data, file.size, &font, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_font_atlas_add(&assets->atlas, &font, 1, assets->glyphs,
		assets->advance, error_out);
	free(font.image.rgba);

	return status;
}

static enum mh_error_code
load_indicator(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_loading_assets *assets, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	struct mh_indexed_image indexed;
	struct mh_image image, resized;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(decoder, resources, root,
		MH_ASSET_LOADING_ANIMATION, &indexed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_loading_indicators_prepare(&indexed, &image, error_out);
	free(indexed.indices);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_loading_image_resize(&image, width,
		height * MH_LOADING_FRAMES, 1, &resized, error_out);
	free(image.rgba);

	if (status != MH_ERROR_NONE)
		return status;

	image = resized;
	status = mh_ui_atlas_add(
		&assets->atlas, &image, &assets->indicator, error_out);
	free(image.rgba);
	assets->indicator.height = height;

	return status;
}

void
mh_loading_assets_free(struct mh_loading_assets *assets)
{
	if (assets == NULL)
		return;

	free(assets->atlas.image.rgba);
	free(assets->word_font.bytes);
	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_loading_assets_load_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	enum mh_render_profile profile, unsigned int width, unsigned int height,
	struct mh_loading_assets *output, struct mh_error *error_out)
{
	struct mh_loading_assets assets;
	struct mh_loading_indicator indicator;
	unsigned int atlas_width, atlas_height;
	enum mh_error_code status;

	if (resources == NULL || root == NULL || output == NULL ||
		width < MH_VIEW_MIN_WIDTH || width > MH_VIEW_MAX_WIDTH ||
		height < MH_VIEW_MIN_HEIGHT || height > MH_VIEW_MAX_HEIGHT ||
		(profile == MH_RENDER_SOFTWARE &&
			width > MH_SOFTWARE_VIEW_MAX_WIDTH) ||
		(unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading assets request");

	atlas_width = ATLAS_WIDTH;
	atlas_height = ATLAS_HEIGHT;
	memset(&indicator, 0, sizeof(indicator));

	if (profile == MH_RENDER_SOFTWARE) {
		status = mh_loading_indicator_layout(
			width, height, 0, 0, &indicator, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Keep the prepared background and indicator strip side by
		 * side.
		 */
		if (width + indicator.width + 2 * IMAGE_PADDING > atlas_width)
			atlas_width =
				width + indicator.width + 2 * IMAGE_PADDING;

		if (height + IMAGE_PADDING > atlas_height)
			atlas_height = height + IMAGE_PADDING;
	}

	memset(&assets, 0, sizeof(assets));
	assets.profile = profile;
	status = mh_ui_atlas_init(
		atlas_width, atlas_height, &assets.atlas, error_out);

	if (status == MH_ERROR_NONE)
		status = load_background(decoder, resources, root, &assets,
			width, height, error_out);

	if (status == MH_ERROR_NONE)
		status = load_font(
			fonts, decoder, resources, root, &assets, error_out);

	if (status == MH_ERROR_NONE && profile == MH_RENDER_SOFTWARE)
		status = load_indicator(decoder, resources, root, &assets,
			indicator.width, indicator.height, error_out);

	if (status != MH_ERROR_NONE) {
		mh_loading_assets_free(&assets);
		return status;
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

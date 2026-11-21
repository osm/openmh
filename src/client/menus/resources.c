#include <stdio.h>
#include <stdlib.h>

#include "client/menus/internal.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/path_menus.h"
#include "engine/core/error.h"
#include "engine/input/device_choices.h"
#include "engine/ui/menu_palette.h"
#include "interfaces/font/source.h"
#include "interfaces/image/palette.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout_source.h"

/* Original padded work allocation and exclusive row-origin clipping bounds. */
#define MENU_WORK_SIZE 614400UL
#define MENU_WORK_PITCH 960
#define MENU_WORK_CLIP_BEGIN 115360UL
#define MENU_WORK_CLIP_END 499360UL
#define MENU_BACKGROUND_RADIUS 160
#define MENU_BACKGROUND_COUNT 3

static enum mh_error_code
load_layout(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	return mh_menu_layout_load_from(menus->layout_source, menus->resources,
		asset_root, MH_ASSET_MENU_LAYOUT, &menus->layout, error_out);
}

static enum mh_error_code
load_backdrop(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	return mh_menu_backdrop_load_from(menus->image_decoder,
		menus->resources, asset_root, &menus->backdrop, error_out);
}

static enum mh_error_code
read_page_art(const struct mh_sprite_collection *sprites,
	struct mh_menus *menus, struct mh_error *error_out)
{
	unsigned char palette[MH_MENU_PALETTE_BYTES];
	enum mh_error_code status;
	unsigned int i;

	status = mh_menu_main_art_read(sprites, &menus->main_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_options_art_read(
			sprites, &menus->options_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_sound_art_read(
			sprites, &menus->sound_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_graphics_art_read(
			sprites, &menus->graphics_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_personal_art_read(
			sprites, &menus->personal_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_credits_art_read(
			sprites, &menus->credits_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_rankings_art_read(
			sprites, &menus->rankings_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_controls_art_read(
			sprites, &menus->controls_art, error_out);

	if (status == MH_ERROR_NONE) {
		menus->controls.preferences = menus->controls_art.preferences;

		/*
		 * Preference can be saved before a wheel is connected.
		 */
		menus->controls.unavailable =
			mh_menus_feature_enabled(
				&menus->configuration, MH_MENU_FEATURE_FEEDBACK)
			? 0
			: (1U << MH_MENU_CONTROLS_FEEDBACK);

		if (!mh_input_scan_available(MH_KEY_MOUSE_LEFT))
			menus->controls.unavailable |=
				(1U << MH_MENU_CONTROLS_MOUSE_X) |
				(1U << MH_MENU_CONTROLS_MOUSE_Y) |
				(1U << MH_MENU_CONTROLS_MOUSE_Z);
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_multi_art_read(
			sprites, &menus->multi_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_single_art_read(
			sprites, &menus->single_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_race_art_read(
			sprites, &menus->race_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_car_art_read(
			sprites, &menus->car_art, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_ghost_art_read(
			sprites, &menus->ghost_art, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < MH_MENUS_LEAGUE_COUNT; i++)
		status = mh_menu_league_art_read(sprites,
			mh_menus_league_destinations[i], &menus->league_art[i],
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_load(sprites, palette, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_base(palette, menus->backdrop.palette,
			menus->palette, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_sprite_collection_indexed(
			sprites, "req", 0, &menus->dialog_sprite, error_out);

	return status;
}

static enum mh_error_code
load_sprites(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	struct mh_sprite_collection *sprites;
	enum mh_error_code status;

	status = mh_sprite_collection_open(menus->sprite_source,
		menus->resources, asset_root, MH_ASSET_MENU_SPRITES,
		MH_PICTURE_INDEXED, &sprites, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = read_page_art(sprites, menus, error_out);
	mh_sprite_collection_close(sprites);

	return status;
}

static enum mh_error_code
load_font(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	return mh_font_load(menus->font_source, menus->resources, asset_root,
		MH_ASSET_FONT1, &menus->font, error_out);
}

static enum mh_error_code
load_personal_colors(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned long colors[MH_PALETTE_COLORS];
	unsigned int i;

	status = mh_palette_load_from(menus->palette_source, menus->resources,
		asset_root, MH_ASSET_MENU_PALETTE, colors, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++)
		menus->personal_colors[i] = colors[MH_PALETTE_COLORS - 1 - i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_noise(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	return mh_menu_noise_load_from(menus->image_decoder, menus->resources,
		asset_root, menus->static_noise, error_out);
}

static enum mh_error_code
load_background_models(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	char relative_path[64];
	unsigned int i;
	float scale;

	for (i = 0; i < MH_MENU_BACKGROUND_MODELS; i++) {
		sprintf(relative_path, MH_ASSET_MENU_LINE_OBJECT_FORMAT, i);
		status = mh_wireframe_load_from(menus->wireframe_source,
			menus->resources, asset_root, relative_path,
			&menus->background_models[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_wireframe_fit(&menus->background_models[i],
				MENU_BACKGROUND_RADIUS, &scale, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
allocate_canvases(struct mh_menus *menus, struct mh_error *error_out)
{

	menus->work.pixels = (unsigned char *)calloc(MENU_WORK_SIZE, 1);
	menus->present.pixels = (unsigned char *)calloc(MH_MENUS_PIXELS, 1);
	menus->rgba = (unsigned char *)malloc(MH_MENUS_PIXELS * 4);

	if (menus->work.pixels == NULL || menus->present.pixels == NULL ||
		menus->rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating menu canvases");

	menus->work.size = MENU_WORK_SIZE;
	menus->work.pitch = MENU_WORK_PITCH;
	menus->work.begin = MENU_WORK_CLIP_BEGIN;
	menus->work.end = MENU_WORK_CLIP_END;
	menus->present.size = menus->present.end = MH_MENUS_PIXELS;
	menus->present.pitch = MH_MENUS_WIDTH;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menus_resources_load(const char *asset_root, const char *progress_directory,
	struct mh_menus *menus, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (mh_menus_compact(menus)) {
		status = load_font(asset_root, menus, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menus_catalogs_load(asset_root,
				progress_directory, menus, error_out);

		if (status == MH_ERROR_NONE)
			status = allocate_canvases(menus, error_out);

		return status;
	}

	status = load_layout(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_backdrop(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_sprites(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_font(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_personal_colors(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_noise(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menus_catalogs_load(
		asset_root, progress_directory, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_background_models(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = allocate_canvases(menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menu_background_reset(
		&menus->background, MENU_BACKGROUND_COUNT, error_out);
}

void
mh_menus_destroy(struct mh_menus *menus)
{
	unsigned int i;

	if (menus == NULL)
		return;

	mh_menus_launch_close(menus);
	mh_menu_audio_close(menus->audio);
	free(menus->profiles);
	mh_leagues_close(menus->leagues);

	for (i = 0; i < MH_MENUS_LEAGUE_COUNT; i++)
		mh_menu_league_art_free(&menus->league_art[i]);

	mh_recording_catalog_close(&menus->recordings);
	mh_demo_file_abort(menus->take);
	mh_menu_main_art_free(&menus->main_art);
	mh_menu_backdrop_free(&menus->backdrop);
	mh_menu_options_art_free(&menus->options_art);
	mh_menu_sound_art_free(&menus->sound_art);
	mh_menu_graphics_art_free(&menus->graphics_art);
	mh_graphics_catalog_close(menus->graphics_catalog);
	mh_menu_personal_art_free(&menus->personal_art);
	mh_menu_credits_art_free(&menus->credits_art);
	mh_menu_rankings_art_free(&menus->rankings_art);
	free(menus->rankings_preferred);
	mh_menu_controls_art_free(&menus->controls_art);
	mh_menu_multi_art_free(&menus->multi_art);
	mh_address_book_free(&menus->addresses);
	mh_address_store_close(menus->address_store);
	mh_transport_close(menus->discovery_transport);
	mh_transport_target_close(menus->connection_target);
	mh_control_catalog_free(&menus->controls_catalog);
	mh_control_store_close(menus->controls_store);
	mh_menu_single_art_free(&menus->single_art);
	mh_menu_race_art_free(&menus->race_art);
	mh_menu_car_art_free(&menus->car_art);
	mh_menu_ghost_art_free(&menus->ghost_art);
	free(menus->font.image.rgba);
	free(menus->dialog_sprite.pixels);

	for (i = 0; i < MH_MENU_BACKGROUND_MODELS; i++)
		mh_wireframe_free(&menus->background_models[i]);

	for (i = 0; i < menus->track_catalog.count; i++) {
		mh_wireframe_free(&menus->track_models[i]);
		mh_menu_thumbnail_free(&menus->track_thumbnails[i]);
	}

	for (i = 0; i < menus->car_catalog.count; i++) {
		mh_wireframe_free(&menus->car_models[i]);
		mh_menu_thumbnail_free(&menus->car_thumbnails[i]);
	}

	mh_car_catalog_close(&menus->car_catalog);
	mh_track_catalog_close(&menus->track_catalog);

	free(menus->work.pixels);
	free(menus->present.pixels);
	free(menus->rgba);
	free(menus);
}

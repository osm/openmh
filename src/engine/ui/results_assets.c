#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_catalog.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/path_menus.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/image/portrait.h"
#include "engine/ui/font_atlas.h"
#include "engine/ui/results_assets.h"
#include "interfaces/catalog/profile_source.h"
#include "interfaces/font/source.h"

#define BACKGROUND_WIDTH 640U
#define BACKGROUND_HEIGHT 400U
#define PLAYER_NAME_LENGTH 15
#define DEFAULT_PLAYER_COLOR 0xffcc88UL

static enum mh_error_code
append_portrait(const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *relative, struct mh_ui_atlas *atlas,
	struct mh_ui_region *region, struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	status = mh_portrait_load_from(
		portrait_decoder, resources, root, relative, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_atlas_add(atlas, &image, region, error_out);
	free(image.rgba);

	return status;
}

static enum mh_error_code
load_background(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, int season, struct mh_ui_atlas *atlas,
	struct mh_ui_region *region, struct mh_error *error_out)
{
	unsigned char pixel[4] = {10, 15, 35, 255};
	struct mh_image image;
	const char *reference;
	enum mh_error_code status;

	/* The original numbered portrait board has only eight places. */
	if (count > MH_RACE_LEGACY_CARS) {
		memset(&image, 0, sizeof(image));
		image.width = image.height = 1;
		image.rgba = pixel;
		return mh_ui_atlas_add(atlas, &image, region, error_out);
	}

	reference = season ? (count <= 2 ? MH_ASSET_LEAGUE_RESULTS_TWO
					: MH_ASSET_LEAGUE_RESULTS)
			: (count <= 2 ? MH_ASSET_RESULTS_TWO_BACKDROP
					: MH_ASSET_RESULTS_BACKDROP);

	status = mh_image_load_from(image_decoder, resources, root, reference,
		NULL, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (image.width != BACKGROUND_WIDTH ||
		image.height != BACKGROUND_HEIGHT)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid results image dimensions");
	else
		status = mh_ui_atlas_add(atlas, &image, region, error_out);

	free(image.rgba);

	return status;
}

static enum mh_error_code
load_font(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	struct mh_ui_atlas *atlas, struct mh_results_assets *results,
	struct mh_error *error_out)
{
	struct mh_font font;
	enum mh_error_code status;

	status = mh_font_load(fonts, resources, root, MH_ASSET_FONT1_LOWERCASE,
		&font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_font_atlas_add(
		atlas, &font, 0, results->glyphs, results->advance, error_out);
	free(font.image.rgba);

	return status;
}

static enum mh_error_code
load_board_assets(const struct mh_profile_source *profile_source,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const char *name, const char *const *names,
	const struct mh_profile *const *profiles, unsigned int human_mask,
	int season, struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out)
{
	struct mh_results_assets results;
	const unsigned char *p;
	unsigned int i;
	char relative[128];
	enum mh_error_code status;

	/* Demonstration roster used only by the standalone driving tool. */
	static const char *demo_profiles[7] = {
		"Rodriges", "Jeepen", "b", "Pa", "boman", "Kenny", "Eq"};

	if (count == 0 || count > MH_RESULTS_MAX_PLAYERS ||
		(names == NULL && profiles == NULL &&
			count > MH_TRAFFIC_MAX_CARS) ||
		name == NULL || strlen(name) > PLAYER_NAME_LENGTH)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"player name must contain at most 15 game text bytes");

	for (i = 0; i < (names == NULL ? 1 : count); i++) {
		const char *name_text;

		name_text = names == NULL ? name : names[i];

		if (name_text == NULL || strlen(name_text) > PLAYER_NAME_LENGTH)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid results player name");

		for (p = (const unsigned char *)name_text; *p != 0; p++)
			if (*p < 32)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"player name contains unsupported control bytes");
	}

	memset(&results, 0, sizeof(results));
	results.count = count;

	for (i = 0; i < count; i++)
		results.profiles[i].colors[0] = DEFAULT_PLAYER_COLOR;

	status = load_background(image_decoder, resources, root, count, season,
		atlas, &results.background, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_font(fonts, resources, root, atlas, &results, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(results.profiles[0].name, name);
	strcpy(results.profiles[0].nick, name);
	strcpy(results.profiles[0].picture, MH_ASSET_PLAYER_PICTURE);

	for (i = 0; i < count; i++) {
		if (profiles != NULL) {
			results.profiles[i] = *profiles[i];

			if (human_mask & (1U << i))
				strcpy(results.profiles[i].picture,
					MH_ASSET_PLAYER_PICTURE);
		} else if (names != NULL) {
			strcpy(results.profiles[i].name, names[i]);
			strcpy(results.profiles[i].nick, names[i]);
			strcpy(results.profiles[i].picture,
				MH_ASSET_PLAYER_PICTURE);

			if (i != 0) {
				results.portraits[i] = results.portraits[0];
				continue;
			}
		} else if (i != 0) {
			status = mh_profile_load_from(profile_source, resources,
				root, demo_profiles[i - 1],
				&results.profiles[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		sprintf(relative, "%s/%s",
			(profiles != NULL ? (human_mask & (1U << i)) != 0
					: i == 0)
				? MH_ASSET_GAME_DIR
				: MH_ASSET_PROFILE_PICTURES_DIR,
			results.profiles[i].picture);
		status = append_portrait(portrait_decoder, resources, root,
			relative, atlas, &results.portraits[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*output = results;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_results_assets_load_from(const struct mh_profile_source *profile_source,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const char *name, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out)
{
	return load_board_assets(profile_source, fonts, image_decoder,
		portrait_decoder, resources, root, count, name, NULL, NULL, 0,
		0, atlas, output, error_out);
}

enum mh_error_code
mh_results_assets_players_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const char *const *names, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out)
{
	if (names == NULL || count == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing results player names");

	return load_board_assets(NULL, fonts, image_decoder, portrait_decoder,
		resources, root, count, names[0], names, NULL, 0, 0, atlas,
		output, error_out);
}

static int
valid_text(const char *s, size_t capacity, int basename)
{
	size_t i;
	unsigned char c;

	/*
	 * Personal can erase the displayed name/nickname. Asset basenames
	 * still require a nonempty, restricted path component.
	 */
	if (basename && (s[0] == 0 || s[0] == '.'))
		return 0;

	for (i = 0; i < capacity; i++) {
		c = (unsigned char)s[i];

		if (c == 0)
			return 1;

		if (c < 32)
			return 0;

		if (basename) {
			if (!((c >= 'a' && c <= 'z') ||
				(c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '.' ||
				c == '_' || c == '-'))
				return 0;

			if (i != 0 && c == '.' && s[i - 1] == '.')
				return 0;
		}
	}

	return 0;
}

static int
valid_profile(const struct mh_profile *profile, int human)
{
	return profile != NULL &&
		valid_text(profile->name, sizeof(profile->name), 0) &&
		valid_text(profile->nick, sizeof(profile->nick), 0) &&
		(human ||
			valid_text(
				profile->picture, sizeof(profile->picture), 1));
}

enum mh_error_code
mh_results_assets_profiles_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output,
	struct mh_error *error_out)
{
	return mh_results_assets_roster_from(fonts, image_decoder,
		portrait_decoder, resources, root, count, profiles, 1, atlas,
		output, error_out);
}

enum mh_error_code
mh_results_assets_roster_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out)
{
	unsigned int i;

	if (root == NULL || profiles == NULL || atlas == NULL ||
		output == NULL || count == 0 ||
		count > MH_RESULTS_MAX_PLAYERS || (human_mask >> count) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid results profiles");

	for (i = 0; i < count; i++) {
		if (!valid_profile(profiles[i], (human_mask & (1U << i)) != 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid results profile presentation");
	}

	return load_board_assets(NULL, fonts, image_decoder, portrait_decoder,
		resources, root, count, profiles[0]->nick, NULL, profiles,
		human_mask, 0, atlas, output, error_out);
}

enum mh_error_code
mh_results_assets_season_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int count, const struct mh_profile *const *profiles,
	unsigned int human_mask, struct mh_ui_atlas *atlas,
	struct mh_results_assets *output, struct mh_error *error_out)
{
	unsigned int i;

	if (root == NULL || profiles == NULL || atlas == NULL ||
		output == NULL || (count != 2 && count != 8) ||
		(human_mask >> count) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid season results profiles");

	for (i = 0; i < count; i++) {
		if (!valid_profile(profiles[i], (human_mask & (1U << i)) != 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid season profile presentation");
	}

	return load_board_assets(NULL, fonts, image_decoder, portrait_decoder,
		resources, root, count, "Player", NULL, profiles, human_mask, 1,
		atlas, output, error_out);
}

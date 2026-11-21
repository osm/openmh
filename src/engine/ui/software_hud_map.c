#include <float.h>
#include <math.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/ui/software_hud.h"

enum {
	MAP_WIDTH = 128,
	MAP_HEIGHT = 160,
	WORLD_LIMIT = 1000000,
	SMALL_LIGHT = 248,
	SMALL_SHADE = 249,
	LARGE_LIGHT = 250,
	LARGE_SHADE = 251
};

static enum mh_error_code
visit_club(const struct mh_software_hud_assets *assets,
	const struct mh_hud_marker *entry, const char *track,
	struct mh_software_hud_secrets *secrets, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	static const unsigned char greeting[] = "WELCOME TO TBL CLUB";
	float x, z;
	long double dz, distance;
	unsigned int shift;
	enum mh_error_code status;

	if (!entry->viewed || track == NULL ||
		!mh_ascii_equal(track, "ruhrstadt"))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* The original cylinder ignores height, even before map projection. */
	if (!(entry->world[0] >= -WORLD_LIMIT &&
		entry->world[0] <= WORLD_LIMIT &&
		entry->world[2] >= -WORLD_LIMIT &&
		entry->world[2] <= WORLD_LIMIT))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	x = (float)((long double)entry->world[0] - 476);
	dz = (long double)entry->world[2] + 188;
	z = (float)dz;

	/* Retain unrounded Z for one factor in the square. */
	distance = (long double)x * x + dz * z;

	if (distance >= 400) {
		secrets->visits = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (secrets->visits > 200) {
		shift = assets->resolution_shift;
		status = mh_ui_word_text(list, &assets->font, greeting,
			sizeof(greeting) - 1, (int)list->width - (390 >> shift),
			(int)list->height - (264 >> shift), MH_U16_MAX,
			MH_UI_TEXT_LEFT, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		secrets->club_credits = 1;
	}

	/* Software keeps counting after activation; hardware stops at 201. */
	secrets->visits = (secrets->visits + 1) & MH_U32_MAX;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
project_marker(const float info[4], const float world[3], int pixel[2])
{
	unsigned int i;
	float dy, x, z;
	long double inverse, angle, sine, cosine, px, py;

	for (i = 0; i < 4; i++)
		if (!(info[i] >= -WORLD_LIMIT && info[i] <= WORLD_LIMIT))
			return 0;

	for (i = 0; i < 3; i++)
		if (!(world[i] >= -WORLD_LIMIT && world[i] <= WORLD_LIMIT))
			return 0;

	dy = (float)((long double)info[1] - world[1]);

	if (dy == 0)
		return 0;

	inverse = 1.0L / dy;
	px = ((long double)world[0] - info[0]) * 256 * inverse;
	py = ((long double)world[2] - info[2]) * 256 * inverse;

	if (!(px >= -FLT_MAX && px <= FLT_MAX && py >= -FLT_MAX &&
		py <= FLT_MAX))
		return 0;

	/* Round map coordinates to float before rotating them. */
	x = (float)px;
	z = (float)py;
	angle = ((long double)info[3] + 90) *
		(long double)MH_HUD_MAP_RADIANS_PER_DEGREE;

	sine = sin((double)angle);
	cosine = cos((double)angle);
	px = sine * x + cosine * z + 64;
	py = cosine * x - sine * z + 80;

	/* Truncate before the unsigned 128x160 gate. */
	if (!(px > -1 && px < MAP_WIDTH && py > -1 && py < MAP_HEIGHT))
		return 0;

	pixel[0] = (int)px;
	pixel[1] = (int)py;

	return 1;
}

static enum mh_error_code
draw_marker(const struct mh_software_hud_assets *assets,
	const struct mh_hud_marker *entry, const int pixel[2],
	struct mh_ui_list *list, struct mh_error *error_out)
{
	static const float white[4] = {1, 1, 1, 1};
	const struct mh_ui_region *region;
	unsigned char glyphs[2];
	unsigned int shift, i;
	int x, y;
	enum mh_error_code status;

	shift = assets->resolution_shift;
	region = NULL;

	if (entry->rank <= MH_SOFTWARE_HUD_RANKS)
		region = entry->viewed
			? &assets->large_markers[entry->rank - 1]
			: &assets->small_markers[entry->rank - 1];

	x = (int)(list->width << shift) - 140 + pixel[0] -
		(entry->viewed ? 8 : 6);
	y = (int)(list->height << shift) - 164 + pixel[1] -
		(entry->viewed ? 30 : 24) - (shift ? 2 : 0);

	/*
	 * Missing rank artwork is a no-op in 3.1; colored glyphs still draw.
	 */
	if (region != NULL &&
		!mh_ui_quad(list, &assets->atlas, region, (float)(x >> shift),
			(float)(y >> shift), white))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software map marker could not append sprite");

	x += shift ? 2 : 0;
	y += shift ? 14 : 13;
	glyphs[0] = entry->viewed ? (shift ? LARGE_SHADE : LARGE_LIGHT)
				: SMALL_LIGHT;

	glyphs[1] = entry->viewed ? (shift ? LARGE_LIGHT : LARGE_SHADE)
				: SMALL_SHADE;

	for (i = 0; i < 2; i++) {
		status = mh_ui_word_text(list, &assets->font, &glyphs[i], 1,
			x >> shift, y >> shift,
			mh_software_hud_color(i
					? (entry->color >> 1) & 0x7f7f7fUL
					: entry->color,
				assets->green_bits),
			MH_UI_TEXT_LEFT, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_map(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, const char *track,
	struct mh_software_hud_secrets *secrets, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	static const float white[4] = {1, 1, 1, 1};
	struct mh_software_hud_secrets next_secrets;
	const struct mh_hud_marker *entry;
	unsigned int i, shift, viewed_rank, saved_count, saved_text_count,
		saved_text_size;
	int pixel[2];
	enum mh_error_code status;

	if (assets == NULL || state == NULL || list == NULL ||
		list->width < MH_HUD_MIN_WIDTH ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		assets->resolution_shift != (unsigned int)(list->width < 640) ||
		(assets->green_bits != 5 && assets->green_bits != 6) ||
		state->map_mode > 2 || state->marker_count > MH_RACE_MAX_CARS ||
		list->count > MH_UI_MAX_QUADS * 6 || list->count % 6 ||
		list->text_count > MH_UI_MAX_TEXTS ||
		list->text_size > MH_UI_TEXT_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD map");

	if (secrets != NULL &&
		(secrets->visits > MH_U32_MAX ||
			(secrets->club_credits != 0 &&
				secrets->club_credits != 1)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD secret state");

	if (state->map_mode == 0 || assets->map.width == 0 ||
		assets->map.height == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	saved_count = list->count;
	saved_text_count = list->text_count;
	saved_text_size = list->text_size;
	shift = assets->resolution_shift;

	if (secrets != NULL)
		next_secrets = *secrets;

	if (!mh_ui_quad(list, &assets->atlas, &assets->map,
		(float)(list->width - (140 >> shift)),
		(float)(list->height - (164 >> shift)), white))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software HUD map could not append sprite");

	viewed_rank = 0;

	for (i = 0; i < state->marker_count; i++)
		if (state->markers[i].viewed)
			viewed_rank = state->markers[i].rank;

	for (i = 0; i < state->marker_count; i++) {
		entry = &state->markers[i];

		if (!entry->active)
			continue;

		if (secrets != NULL) {
			status = visit_club(assets, entry, track, &next_secrets,
				list, error_out);

			if (status != MH_ERROR_NONE)
				goto failed;
		}

		if (entry->rank < 1 || entry->rank > MH_RACE_MAX_CARS)
			continue;

		if (state->map_mode == 1 && entry->rank != viewed_rank &&
			entry->rank + 1 != viewed_rank &&
			entry->rank != viewed_rank + 1)
			continue;

		if (!project_marker(assets->map_info, entry->world, pixel))
			continue;

		status = draw_marker(assets, entry, pixel, list, error_out);

		if (status != MH_ERROR_NONE)
			goto failed;
	}

	if (secrets != NULL)
		*secrets = next_secrets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

failed:
	list->count = saved_count;
	list->text_count = saved_text_count;
	list->text_size = saved_text_size;

	return status;
}

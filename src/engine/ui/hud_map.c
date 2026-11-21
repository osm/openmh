#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/render/scene_matrices.h"
#include "engine/ui/hud.h"

#define WORLD_COORDINATE_LIMIT 1000000
#define PROJECTION_PIXEL_LIMIT 16000

void
mh_hud_map_state(const struct mh_field *field, const float *models,
	const struct mh_profile *profiles, struct mh_hud_state *state)
{
	struct mh_hud_marker *entry;
	unsigned int i;

	state->marker_count =
		field->count <= MH_RACE_MAX_CARS ? field->count : 0;

	for (i = 0; i < state->marker_count; i++) {
		entry = &state->markers[i];
		memset(entry, 0, sizeof(*entry));
		memcpy(entry->world, models + (1 + MH_CAR_MESHES * i) * 16 + 12,
			sizeof(entry->world));
		entry->active = (field->active_mask & (1U << i)) != 0;
		entry->viewed = i == mh_field_viewed(field);
		entry->rank = field->standings.cars[i].position;
		entry->color =
			profiles == NULL ? 0xffcc88UL : profiles[i].colors[0];
		entry->special = profiles != NULL &&
			mh_ascii_equal(profiles[i].nick, "avenger") &&
			mh_ascii_equal(profiles[i].team, "zx");
	}
}

int
mh_hud_map_project(const float info[4], const float offset[2],
	const float world[3], int origin_x, int origin_y, int pixel[2])
{
	float dy, x, z;
	long double angle, sine, cosine, px, py;
	unsigned int i;

	if (info == NULL || offset == NULL || world == NULL || pixel == NULL)
		return 0;

	for (i = 0; i < 4; i++)
		if (!(info[i] >= -WORLD_COORDINATE_LIMIT &&
			info[i] <= WORLD_COORDINATE_LIMIT))
			return 0;

	for (i = 0; i < 3; i++)
		if (!(world[i] >= -WORLD_COORDINATE_LIMIT &&
			world[i] <= WORLD_COORDINATE_LIMIT))
			return 0;

	for (i = 0; i < 2; i++)
		if (!(offset[i] >= -MH_HUD_VIEW_LIMIT &&
			offset[i] <= MH_HUD_VIEW_LIMIT))
			return 0;

	dy = (float)((long double)info[1] - world[1]);

	if (dy == 0)
		return 0;

	px = ((long double)world[0] - info[0]) * 256 / dy;
	py = ((long double)world[2] - info[2]) * 256 / dy;

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
	px = (sine * x + cosine * z + 64) * .5L + origin_x + offset[0];
	py = (cosine * x - sine * z + 80) * .5L + origin_y + offset[1];

	if (!(px >= -PROJECTION_PIXEL_LIMIT && px <= PROJECTION_PIXEL_LIMIT &&
		py >= -PROJECTION_PIXEL_LIMIT &&
		py <= PROJECTION_PIXEL_LIMIT))
		return 0;

	pixel[0] = (int)px;
	pixel[1] = (int)py;

	return 1;
}

int
mh_hud_map_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	static const float white[4] = {1, 1, 1, 1};
	const struct mh_hud_marker *entry;
	const struct mh_ui_region *marker, *number;
	float color[4];
	unsigned int i, viewed_rank, id;
	int origin_x, origin_y, pixel[2], x, y;

	if (state->map_mode > 2 || state->marker_count > MH_RACE_MAX_CARS)
		return 0;

	if (state->map_mode == 0 || assets->map.width == 0 ||
		assets->map.height == 0)
		return 1;

	origin_x = (int)list->width - (int)assets->map.width - 10;
	origin_y = (int)list->height - (int)assets->map.height - 10;

	if (!mh_ui_quad(list, &assets->atlas, &assets->map, (float)origin_x,
		(float)origin_y, white))
		return 0;

	viewed_rank = 0;

	for (i = 0; i < state->marker_count; i++)
		if (state->markers[i].viewed)
			viewed_rank = state->markers[i].rank;

	for (i = 0; i < state->marker_count; i++) {
		entry = &state->markers[i];

		if (!entry->active || entry->rank < 1 ||
			entry->rank > MH_RACE_MAX_CARS)
			continue;

		if (state->map_mode == 1 && entry->rank != viewed_rank &&
			entry->rank + 1 != viewed_rank &&
			entry->rank != viewed_rank + 1)
			continue;

		if (!mh_hud_map_project(assets->map_info, assets->map_offset,
			entry->world, origin_x, origin_y, pixel))
			continue;

		if (entry->special)
			id = MH_HUD_AVENGER;
		else if (entry->viewed)
			id = MH_HUD_MARKER;
		else
			id = MH_HUD_OTHER_MARKER;

		marker = &assets->sprites[id];

		/*
		 * 3.1 indexes past mapnr8 into the following catalog entries.
		 */
		if (entry->rank <= MH_HUD_MAP_RANKS)
			id = MH_HUD_MAP_NUMBER + entry->rank - 1;
		else if (entry->rank == 9)
			id = MH_HUD_OTHER_MARKER;
		else
			id = MH_HUD_BIG + entry->rank - 10;

		number = &assets->sprites[id];
		x = pixel[0] - (int)(marker->width / 2);
		y = pixel[1] - (int)marker->height;

		/*
		 * The original widens these subtractions as unsigned integers.
		 */
		if (x < 0 || y < 0 || x > (int)list->width ||
			y > (int)list->height)
			continue;

		color[0] = ((entry->color >> 16) & 255) / 255.0f;
		color[1] = ((entry->color >> 8) & 255) / 255.0f;
		color[2] = (entry->color & 255) / 255.0f;
		color[3] = 1;

		if (!mh_ui_quad(list, &assets->atlas, marker, (float)x,
			(float)y, color))
			return 0;

		x += (int)(marker->width / 2) - (int)(number->width / 2);
		y -= (int)number->height + 1;

		if (y >= 0 &&
			!mh_ui_quad(list, &assets->atlas, number, (float)x,
				(float)y, white))
			return 0;
	}

	return 1;
}

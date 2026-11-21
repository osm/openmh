#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/hud.h"
#include "engine/ui/race_info.h"

#define SEPARATOR_DASHES 69

enum mh_error_code
mh_race_info_state(const struct mh_field *field,
	const struct mh_profile *profiles, const unsigned long *control_types,
	struct mh_race_info *output, struct mh_error *error_out)
{
	struct mh_race_info next;
	struct mh_race_info_row *row;
	const char *name;
	unsigned int i, entry, seen_mask;

	if (field == NULL || output == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS ||
		field->standings.count != field->count ||
		(field->active_mask >> field->count) != 0 ||
		field->cars[0].track == NULL ||
		memchr(field->cars[0].track->name, 0, sizeof(next.track)) ==
			NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Info roster");

	memset(&next, 0, sizeof(next));
	strcpy(next.track, field->cars[0].track->name);
	seen_mask = 0;

	for (i = 0; i < field->count; i++) {
		entry = field->standings.order[i];

		if (entry >= field->count || (seen_mask & (1U << entry)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Info classification");

		seen_mask |= 1U << entry;

		if (!(field->active_mask & (1U << entry)))
			continue;

		row = &next.rows[next.count++];
		row->entry = entry;

		if (profiles == NULL && field->cars[entry].car == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing Info identity");

		name = profiles != NULL ? profiles[entry].nick
					: field->cars[entry].car->name;

		if (memchr(name, 0,
			profiles != NULL ? sizeof(profiles[entry].nick)
					: sizeof(row->name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Info name");

		strcpy(row->name, name);

		if (profiles != NULL) {
			if (memchr(profiles[entry].team, 0,
				sizeof(row->team)) == NULL)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Info team");

			strcpy(row->team, profiles[entry].team);
		}

		row->position = field->standings.cars[entry].position;
		row->color = profiles != NULL ? profiles[entry].colors[0]
					: 0xffcc88UL;
		row->automatic =
			field->cars[entry].config.options.manual ? 0 : 1;
		row->control_type =
			control_types != NULL ? control_types[entry] : 0;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
text_color(unsigned long rgb, float color[4])
{
	color[0] = ((rgb >> 16) & 255) / 255.0f;
	color[1] = ((rgb >> 8) & 255) / 255.0f;
	color[2] = (rgb & 255) / 255.0f;
	color[3] = 1;
}

int
mh_race_info_draw_separator(const struct mh_race_info_line *line,
	const struct mh_ui_atlas *atlas, const struct mh_ui_region *solid,
	struct mh_ui_list *list)
{
	unsigned int i, start, end, gap, saved_count;
	float left, color[4];

	if (line == NULL || list == NULL ||
		line->separator_width < 2 * SEPARATOR_DASHES ||
		line->separator_width > MH_HUD_VIEW_LIMIT)
		return 0;

	left = (float)line->x - (float)(line->separator_width / 2);
	text_color(line->color, color);
	saved_count = list->count;

	/* Retain the original 69 dashes, scaling their span with the table. */
	for (i = 0; i < SEPARATOR_DASHES; i++) {
		start = i * line->separator_width / SEPARATOR_DASHES;
		end = (i + 1) * line->separator_width / SEPARATOR_DASHES;
		gap = (end - start) / 7;

		if (gap == 0)
			gap = 1;

		if (!mh_ui_quad_scaled(list, atlas, solid, left + start,
			(float)line->y, (float)(end - start - gap), 1,
			color)) {
			list->count = saved_count;
			return 0;
		}
	}

	return 1;
}

static int
draw_centered_text(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int x, int y, unsigned long rgb)
{
	const unsigned char *p;
	unsigned int width;
	float color[4];

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += assets->hardware_advance[*p];

	x -= (int)(width / 2);
	text_color(rgb, color);

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!mh_ui_quad(list, &assets->atlas,
			&assets->hardware_glyphs[*p], (float)x, (float)y,
			color))
			return 0;

		x += (int)assets->hardware_advance[*p];
	}

	return 1;
}

int
mh_race_info_draw(const struct mh_hud_assets *assets,
	const struct mh_race_info *info, struct mh_ui_list *list)
{
	struct mh_race_info_layout layout;
	const struct mh_race_info_line *line;
	unsigned int i, saved_count;
	int drawn;

	if (assets == NULL || list == NULL || list->count > MH_UI_MAX_QUADS * 6)
		return 0;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (assets->hardware_advance[i] > MH_HUD_FONT_LIMIT)
			return 0;

	if (mh_race_info_layout(info, list->width, list->height,
		assets->hardware_height, &layout, NULL) != MH_ERROR_NONE)
		return 0;

	saved_count = list->count;

	for (i = 0; i < layout.count; i++) {
		line = &layout.lines[i];

		if (line->separator_width != 0)
			drawn = mh_race_info_draw_separator(
				line, &assets->atlas, &assets->solid, list);
		else
			drawn = draw_centered_text(assets, list, line->text,
				line->x, line->y, line->color);

		if (!drawn) {
			list->count = saved_count;
			return 0;
		}
	}

	return 1;
}

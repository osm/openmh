#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_records.h"
#include "engine/ui/hud.h"
#include "engine/ui/race_gaps.h"

#define GAP_TIME_BYTES 32

enum mh_error_code
mh_hud_gap_state(const struct mh_field *field,
	const struct mh_profile *profiles, struct mh_hud_state *state,
	struct mh_error *error_out)
{
	const struct mh_race *cars[MH_GAPS_MAX_CARS];
	struct mh_race_gaps gaps;
	struct mh_hud_gap rows[MH_GAPS_MAX_CARS];
	enum mh_error_code status;
	const char *name;
	unsigned int i, entry;

	if (field == NULL || state == NULL || field->count > MH_GAPS_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid HUD gap roster");

	if (field->time_attack)
		return mh_hud_record_gap_state(field, 0, state, error_out);

	if (!state->timed) {
		state->gap_count = 0;
		state->record_gaps = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < field->count; i++)
		cars[i] = &field->cars[i].race;

	status = mh_race_gaps(cars, field->count, field->active_mask,
		mh_field_viewed(field), state->gap_mode, &gaps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(rows, 0, sizeof(rows));

	for (i = 0; i < gaps.count; i++) {
		entry = gaps.rows[i].entry;

		if (profiles == NULL && field->cars[entry].car == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing HUD gap identity");

		name = profiles != NULL ? profiles[entry].nick
					: field->cars[entry].car->name;

		if (memchr(name, 0,
			profiles != NULL ? sizeof(profiles[entry].nick)
					: sizeof(rows[i].name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid HUD gap name");

		strcpy(rows[i].name, name);
		rows[i].color = profiles != NULL ? profiles[entry].colors[0]
						: 0xffcc88UL;
		rows[i].milliseconds = gaps.rows[i].milliseconds;
		rows[i].position = gaps.rows[i].position;
	}

	memcpy(state->gaps, rows, sizeof(rows));
	state->gap_count = gaps.count;
	state->gap_mode = gaps.mode;
	state->record_gaps = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_hud_record_gap_state(const struct mh_field *field, int suppressed,
	struct mh_hud_state *state, struct mh_error *error_out)
{
	const struct mh_race *race;
	struct mh_record_gaps gaps;
	struct mh_hud_gap rows[MH_RACE_MAX_CARS];
	unsigned int viewed, mode, count;
	enum mh_error_code status;

	if (field == NULL || state == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS || state->gap_mode > 2 ||
		(suppressed != 0 && suppressed != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid HUD record gaps");

	viewed = mh_field_viewed(field);

	if ((field->active_mask >> field->count) != 0 ||
		!(field->active_mask & (1U << viewed)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid HUD record gap reference");

	if (!state->timed) {
		state->gap_count = 0;
		state->record_gaps = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	race = &field->cars[viewed].race;

	if (race->split_hold_ms > 10000 || race->split_notice > 3 ||
		(race->finished != 0 && race->finished != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid HUD record gap notice");

	mode = race->finished ? 2 : state->gap_mode;
	count = 0;
	memset(rows, 0, sizeof(rows));

	if (mode != 0 && race->split_hold_ms != 0 && race->split_notice != 0 &&
		!suppressed) {
		status = mh_race_record_gaps(
			&field->records, race, &gaps, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		strcpy(rows[0].name, "Lap");
		rows[0].color = 0xffffffUL;
		strcpy(rows[1].name, "Lap Ever");
		rows[1].color = 0xaaaa00UL;
		strcpy(rows[2].name, "Race Ever");
		rows[2].color = 0xaaaa00UL;
		rows[0].milliseconds = gaps.personal_lap;
		rows[1].milliseconds = gaps.global_lap;
		rows[2].milliseconds = gaps.global_race;
		count = 3;
	}

	memcpy(state->gaps, rows, sizeof(rows));
	state->gap_count = count;
	state->gap_mode = mode;
	state->record_gaps = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
text_width(const struct mh_hud_assets *assets, const char *text)
{
	const unsigned char *p;
	int width;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += (int)assets->hardware_advance[*p];

	return width;
}

static int
draw_text(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int x, int y, const float color[4])
{
	const unsigned char *p;

	/* HUD exit disables the font shadow before the gap callbacks. */
	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!mh_ui_quad(list, &assets->atlas,
			&assets->hardware_glyphs[*p], (float)x, (float)y,
			color))
			return 0;

		x += (int)assets->hardware_advance[*p];
	}

	return 1;
}

static void
format_gap_time(long milliseconds, char text[GAP_TIME_BYTES])
{
	const char *sign;
	unsigned long magnitude;

	if (milliseconds == MH_I32_MIN) {
		/*
		 * Negation wraps at INT32_MIN; signed division and digit writes
		 * produce these glyphs. Preserve them without signed C
		 * overflow.
		 */
		strcpy(text, "-/:.-:*,");
	} else {
		magnitude = (unsigned long)(milliseconds < 0 ? -milliseconds
							: milliseconds);
		sign = milliseconds < 0 ? "-" : milliseconds > 0 ? "+" : "";
		sprintf(text, "%s%lu:%02lu:%02lu", sign, magnitude / 60000,
			(magnitude / 1000) % 60, (magnitude / 10) % 100);
	}
}

int
mh_hud_gaps_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	const struct mh_hud_gap *row;
	char clock[GAP_TIME_BYTES], rank[16];
	const char *time_template;
	float color[4];
	unsigned int i;
	int y, right, name_x, reserved_rows;

	if (assets == NULL || state == NULL || list == NULL ||
		state->gap_count > MH_RACE_MAX_CARS || state->gap_mode > 2 ||
		(state->record_gaps != 0 && state->record_gaps != 1) ||
		list->height > MH_HUD_VIEW_LIMIT ||
		assets->hardware_height > MH_HUD_FONT_LIMIT)
		return 0;

	if (state->gap_count == 0)
		return 1;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (assets->hardware_advance[i] > MH_HUD_FONT_LIMIT)
			return 0;

	if (state->gap_count > MH_RACE_LEGACY_CARS)
		reserved_rows = (int)state->gap_count + 2;
	else if (state->gap_mode == 1 || state->record_gaps)
		reserved_rows = 5;
	else
		reserved_rows = 10;

	y = (int)list->height - reserved_rows * (int)assets->hardware_height -
		2;

	for (i = 0; i < state->gap_count; i++) {
		row = &state->gaps[i];

		if (row->position > MH_RACE_MAX_CARS ||
			row->milliseconds < MH_I32_MIN ||
			row->milliseconds > MH_I32_MAX ||
			memchr(row->name, 0, sizeof(row->name)) == NULL)
			return 0;

		y += (int)assets->hardware_height;
		color[0] = ((row->color >> 16) & 255) / 255.0f;
		color[1] = ((row->color >> 8) & 255) / 255.0f;
		color[2] = (row->color & 255) / 255.0f;
		color[3] = 1;

		if (row->position == 0)
			time_template = "+000:00:00";
		else if (row->position < 10)
			time_template = "8. +000:00:00";
		else
			time_template = "13. +000:00:00";

		right = text_width(assets, time_template);
		name_x = right + text_width(assets, "  ");

		if (row->position != 0) {
			sprintf(rank, "%u.", row->position);

			if (!draw_text(assets, list, rank, 4, y, color))
				return 0;
		}

		if (!draw_text(assets, list, row->name, name_x, y, color))
			return 0;

		format_gap_time(row->milliseconds, clock);

		if (!draw_text(assets, list, clock,
			right - text_width(assets, clock), y, color))
			return 0;
	}

	return 1;
}

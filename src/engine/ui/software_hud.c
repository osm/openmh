#include "engine/core/limits.h"
#include "engine/ui/software_hud.h"

enum {
	SMALL_STEP = 20,
	BIG_STEP = 24,
	SPEED_INSET = 8,
	TIMER_ROW = 40,
	SPRITE_LIMIT = 2048,
	BLINK_INTERVAL_MS = 200,
	LAP_NOTICE_MS = 5000
};

static const float white[4] = {1, 1, 1, 1};

static enum mh_error_code
draw_sprite(const struct mh_software_hud_assets *assets,
	struct mh_ui_list *list, unsigned int id, int x, int y)
{
	return mh_ui_quad(list, &assets->atlas, &assets->sprites[id], (float)x,
		(float)y, white)
		? MH_ERROR_NONE
		: MH_ERROR_RANGE;
}

static enum mh_error_code
draw_scaled_sprite(const struct mh_software_hud_assets *assets,
	struct mh_ui_list *list, unsigned int id, int x, int y)
{
	unsigned int shift;

	shift = assets->resolution_shift;

	/* Reduced resolutions discard sprites with negative origins. */
	if (shift && (x < 0 || y < 0))
		return MH_ERROR_NONE;

	return draw_sprite(
		assets, list, id, x / (1 << shift), y / (1 << shift));
}

static enum mh_error_code
draw_number(const struct mh_software_hud_assets *assets,
	struct mh_ui_list *list, unsigned long value, int *right, int y,
	unsigned int base, int step)
{
	do {
		*right -= step;

		if (draw_scaled_sprite(assets, list,
			base + (unsigned int)(value % 10), *right,
			y) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		value /= 10;
	} while (value != 0);

	return MH_ERROR_NONE;
}

static enum mh_error_code
draw_clock(const struct mh_software_hud_assets *assets, struct mh_ui_list *list,
	unsigned long ms, int right, int y)
{
	unsigned long value;
	unsigned int digits[4], i;
	int negative;

	negative = ms >= MH_U32_SIGN_BIT;
	value = negative ? (0UL - ms) & MH_U32_MAX : ms;
	value /= 10;

	/*
	 * Negating INT_MIN wraps, then signed division truncates the
	 * still-negative value.
	 */
	if (ms == MH_U32_SIGN_BIT)
		value = (0UL - value) & MH_U32_MAX;

	digits[0] = (unsigned int)(value % 10);
	value /= 10;
	digits[1] = (unsigned int)(value % 10);
	value /= 10;
	digits[2] = (unsigned int)(value % 10);
	value /= 10;
	digits[3] = (unsigned int)(value % 6);
	value /= 6;

	for (i = 0; i < 4; i++) {
		right -= SMALL_STEP;

		if (draw_scaled_sprite(assets, list,
			MH_SOFTWARE_HUD_SMALL + digits[i], right,
			y) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		if (i == 1 || i == 3) {
			right -= SMALL_STEP / 2;

			if (draw_scaled_sprite(assets, list,
				MH_SOFTWARE_HUD_COLON, right,
				y) != MH_ERROR_NONE)
				return MH_ERROR_RANGE;
		}
	}

	if (draw_number(assets, list, value, &right, y, MH_SOFTWARE_HUD_SMALL,
		SMALL_STEP) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (!negative)
		return MH_ERROR_NONE;

	return draw_scaled_sprite(
		assets, list, MH_SOFTWARE_HUD_MINUS, right - SMALL_STEP, y);
}

static enum mh_error_code
draw_ratio(const struct mh_software_hud_assets *assets, struct mh_ui_list *list,
	unsigned int current, unsigned int total, int right, int y)
{
	if (draw_number(assets, list, total, &right, y, MH_SOFTWARE_HUD_SMALL,
		SMALL_STEP) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	/* Subtract the loaded pixel width before coordinate scaling. */
	right -= (int)assets->sprites[MH_SOFTWARE_HUD_SLASH].width + 6;

	if (draw_scaled_sprite(assets, list, MH_SOFTWARE_HUD_SLASH, right, y) !=
		MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	return draw_number(assets, list, current, &right, y,
		MH_SOFTWARE_HUD_SMALL, SMALL_STEP);
}

static enum mh_error_code
draw_meter(const struct mh_software_hud_assets *assets, struct mh_ui_list *list,
	double revs)
{
	struct mh_ui_region region;
	unsigned int fill, shift;

	shift = assets->resolution_shift;
	region = assets->sprites[MH_SOFTWARE_HUD_METER];

	/* Truncate extended width times the packet's binary32 RPM. */
	fill = (unsigned int)((long double)region.width * (float)revs);
	region.width = fill;

	if (!mh_ui_quad(list, &assets->atlas, &region, (float)(20 >> shift),
		(float)(55 >> shift), white))
		return MH_ERROR_RANGE;

	region = assets->sprites[MH_SOFTWARE_HUD_METER_EMPTY];
	region.x += fill;
	region.width -= fill;

	return mh_ui_quad(list, &assets->atlas, &region,
		(float)((20 >> shift) + fill), (float)(55 >> shift),
		white)
		? MH_ERROR_NONE
		: MH_ERROR_RANGE;
}

static enum mh_error_code
draw_gauges(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	int right;

	if (state->info_detail == 0)
		return MH_ERROR_NONE;

	right = 100 - SPEED_INSET;

	if (draw_number(assets, list, state->speed, &right, 10,
		MH_SOFTWARE_HUD_BIG, BIG_STEP) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (state->info_detail > 1 &&
		draw_meter(assets, list, state->revs) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (draw_scaled_sprite(assets, list,
		state->system == MH_UNITS_IMPERIAL ? MH_SOFTWARE_HUD_MPH
						: MH_SOFTWARE_HUD_KMH,
		100, 34) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (state->info_detail == 1)
		return MH_ERROR_NONE;

	if (draw_scaled_sprite(assets, list, MH_SOFTWARE_HUD_GEAR, 10, 55) !=
		MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	right = 80;

	if (state->gear != 0)
		return draw_number(assets, list, state->gear, &right, 55,
			MH_SOFTWARE_HUD_SMALL, SMALL_STEP);

	/* Scale the step before subtracting, then scale again. */

	return draw_scaled_sprite(assets, list, MH_SOFTWARE_HUD_REVERSE_SMALL,
		right - (SMALL_STEP >> assets->resolution_shift), 55);
}

static enum mh_error_code
draw_timer_labels(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	static const unsigned int ids[] = {MH_SOFTWARE_HUD_TOTAL_TIME,
		MH_SOFTWARE_HUD_LAP_TIME, MH_SOFTWARE_HUD_BEST_LAP_TIME,
		MH_SOFTWARE_HUD_LAP, MH_SOFTWARE_HUD_POSITION};

	unsigned int i, shift;
	int right, y;

	shift = assets->resolution_shift;
	right = (int)list->width - (16 >> shift);
	y = 10;

	for (i = 0; i < 5; i++) {
		if (state->info_detail == 0 ||
			(state->info_detail == 1 && i != 1 && i != 3))
			continue;

		if (draw_sprite(assets, list, ids[i],
			right - (int)assets->sprites[ids[i]].width,
			y >> shift) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		y += TIMER_ROW;
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
draw_timer_values(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	int right, y;

	if (state->info_detail == 0)
		return MH_ERROR_NONE;

	right = (int)(list->width << assets->resolution_shift) - 12;
	y = 30;

	if (state->info_detail > 1) {
		if (draw_clock(assets, list, state->total_ms, right, y) !=
			MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		y += TIMER_ROW;
	}

	if (draw_clock(assets, list, state->lap_ms, right, y) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	y += TIMER_ROW;

	if (state->info_detail > 1) {
		if (draw_clock(assets, list, state->best_ms, right, y) !=
			MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		y += TIMER_ROW;
	}

	if (draw_ratio(assets, list, state->lap, state->laps, right, y) !=
		MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (state->info_detail == 1)
		return MH_ERROR_NONE;

	return draw_ratio(assets, list, state->position, state->cars, right,
		y + TIMER_ROW);
}

static enum mh_error_code
draw_centered_label(const struct mh_software_hud_assets *assets,
	struct mh_ui_list *list, unsigned int id, int y)
{
	return draw_sprite(assets, list, id,
		(int)(list->width / 2) - (int)(assets->sprites[id].width / 2),
		y >> assets->resolution_shift);
}

static enum mh_error_code
draw_notices(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	unsigned int id;
	int y, right, blink, font_height;

	blink = (int)((state->clock_ms / BLINK_INTERVAL_MS) & 1UL);
	font_height = (int)(assets->font.height << assets->resolution_shift);
	y = font_height + 10;

	if (state->info_detail > 1) {
		if (state->wrong_way && !blink &&
			draw_centered_label(assets, list,
				MH_SOFTWARE_HUD_WRONG_WAY, y) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		y += 24;
	}

	if (state->info_detail == 0 || state->checkpoint_info == 0 ||
		!state->time_notices)
		return MH_ERROR_NONE;

	right = (int)((list->width << assets->resolution_shift) / 2) +
		SMALL_STEP * 3;

	if ((state->lap > 1 && state->lap_ms < LAP_NOTICE_MS) ||
		state->race_notice) {
		if (state->lap_notice == 2)
			id = MH_SOFTWARE_HUD_BEST_LAP_EVER;
		else if (state->lap_notice == 1)
			id = MH_SOFTWARE_HUD_BEST_LAP_TIME;
		else
			id = MH_SOFTWARE_HUD_LAST_LAP_TIME;

		if ((state->lap_notice != 2 || !blink) &&
			(draw_centered_label(assets, list, id, y) !=
					MH_ERROR_NONE ||
				draw_clock(assets, list, state->last_ms, right,
					y + 24) != MH_ERROR_NONE))
			return MH_ERROR_RANGE;
	} else if (state->split_ms != 0 &&
		draw_clock(assets, list, state->split_ms, right, y) !=
			MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	y += 30 + font_height;

	if (state->race_notice == 0 || (state->race_notice == 2 && blink))
		return MH_ERROR_NONE;

	id = state->race_notice == 2 ? MH_SOFTWARE_HUD_BEST_RACE_EVER
				: MH_SOFTWARE_HUD_RACE_TIME;

	if (draw_centered_label(assets, list, id, y) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	return draw_clock(assets, list, state->total_ms, right, y + 24);
}

static enum mh_error_code
draw_status_icons(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	int right, y;

	if (state->info_detail == 0 ||
		((state->clock_ms / BLINK_INTERVAL_MS) & 1UL))
		return MH_ERROR_NONE;

	right = (int)list->width;
	y = (int)list->height - 20;

	if ((state->status_flags & MH_HUD_STATUS_RECORDING) &&
		draw_sprite(assets, list, MH_SOFTWARE_HUD_CAMERA, right - 20,
			y) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if ((state->status_flags & MH_HUD_STATUS_GHOST) &&
		draw_sprite(assets, list, MH_SOFTWARE_HUD_GHOST, right - 36,
			y) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (!(state->status_flags & MH_HUD_STATUS_DEMO))
		return MH_ERROR_NONE;

	return draw_sprite(
		assets, list, MH_SOFTWARE_HUD_REVERSE, right - 36, y);
}

enum mh_error_code
mh_software_hud_countdown(const struct mh_software_hud_assets *assets,
	enum mh_start_phase phase, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	unsigned int id;

	if (assets == NULL || list == NULL || phase < MH_START_READY ||
		phase > MH_START_RACING || list->width < MH_HUD_MIN_WIDTH ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		list->count > MH_UI_MAX_QUADS * 6 || list->count % 6 ||
		assets->resolution_shift != (unsigned int)(list->width < 640))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD countdown");

	if (phase < MH_START_THREE || phase > MH_START_GO)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	id = MH_SOFTWARE_HUD_THREE + (unsigned int)(phase - MH_START_THREE);

	if (assets->sprites[id].width > SPRITE_LIMIT ||
		draw_sprite(assets, list, id,
			(int)(list->width / 2) -
				(int)(assets->sprites[id].width / 2),
			(int)(list->height / 8)) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software HUD countdown could not append sprite");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_panel(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	unsigned int saved_count, i;
	const struct mh_ui_region *region;

	if (assets == NULL || state == NULL || list == NULL ||
		list->width < MH_HUD_MIN_WIDTH ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		list->count > MH_UI_MAX_QUADS * 6 || list->count % 6 ||
		assets->resolution_shift != (unsigned int)(list->width < 640) ||
		assets->font.height > 64 || state->info_detail > 2 ||
		state->checkpoint_info > 2 || state->lap_notice > 2 ||
		state->race_notice > 2 || state->speed > 999 ||
		state->system > MH_UNITS_IMPERIAL || state->gear > 9 ||
		state->lap > 99 || state->laps > 99 || state->position > 99 ||
		state->cars > 99 || !(state->revs >= 0 && state->revs <= 1) ||
		state->total_ms > MH_U32_MAX || state->lap_ms > MH_U32_MAX ||
		state->best_ms > MH_U32_MAX || state->last_ms > MH_U32_MAX ||
		state->split_ms > MH_U32_MAX || state->clock_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD panel");

	for (i = 0; i < MH_SOFTWARE_HUD_SPRITES; i++) {
		region = &assets->sprites[i];

		if (region->width > SPRITE_LIMIT ||
			region->height > SPRITE_LIMIT)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software HUD sprite dimensions exceed atlas limit");
	}

	if (assets->sprites[MH_SOFTWARE_HUD_METER].width !=
		assets->sprites[MH_SOFTWARE_HUD_METER_EMPTY].width)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"software HUD meter frames differ in width");

	saved_count = list->count;

	if (draw_gauges(assets, state, list) != MH_ERROR_NONE ||
		draw_timer_labels(assets, state, list) != MH_ERROR_NONE ||
		draw_timer_values(assets, state, list) != MH_ERROR_NONE ||
		draw_notices(assets, state, list) != MH_ERROR_NONE ||
		draw_status_icons(assets, state, list) != MH_ERROR_NONE) {
		list->count = saved_count;
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software HUD panel could not append sprites");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_instruments(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, const char *track,
	struct mh_ui_list *list, struct mh_software_hud_secrets *secrets,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (state == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing HUD state");

	status = MH_ERROR_NONE;

	if (!state->show_info) {
		/* Submit the map before the instrument panel. */
		status = mh_software_hud_map(
			assets, state, track, secrets, list, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_software_hud_panel(
				assets, state, list, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_software_hud_gaps(assets, state, list, error_out);

	if (status == MH_ERROR_NONE && state->show_info)
		status = mh_software_hud_info(
			assets, &state->info, list, error_out);

	return status;
}

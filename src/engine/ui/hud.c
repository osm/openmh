#include <math.h>
#include <string.h>

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/ui/chat_draw.h"
#include "engine/ui/hud.h"

#define SPEED_LIMIT 999
#define BLINK_INTERVAL_MS 200
#define LAP_NOTICE_MS 5000
#define STATUS_RETAIN_MASK 0xfffffff8UL

static const float white[4] = {1, 1, 1, 1};
static const float dim[4] = {127.0f / 255, 127.0f / 255, 127.0f / 255, 1};
static const float black[4] = {0, 0, 0, 1};

unsigned int
mh_hud_display_speed(float speed, unsigned int system)
{
	/*
	 * European 3.1 speed factors; keep extended product precision until
	 * truncation.
	 */
	static const float factors[] = {1.0f, 0.6213712096214294f};
	long double value;

	value = (long double)speed * factors[system == MH_UNITS_IMPERIAL];

	if (value >= SPEED_LIMIT)
		return SPEED_LIMIT;

	return value > 0 ? (unsigned int)value : 0;
}

enum mh_error_code
mh_hud_state(const struct mh_field *field, int paused,
	struct mh_hud_state *state, struct mh_error *error_out)
{
	unsigned int i;
	const struct mh_drive *drive;
	float revs;
	unsigned long clock_ms;
	enum mh_error_code status;

	if (field == NULL || state == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid HUD snapshot field");

	/*
	 * Use the process game clock, independent of the viewed entrant's
	 * lap/playback time. The local field owns that live sample.
	 */
	status = mh_clock_word(
		mh_drive_clock_ms(&field->cars[0]), &clock_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	drive = &field->cars[mh_field_viewed(field)];
	memset(state, 0, sizeof(*state));
	state->info_detail = 2;
	state->speed = mh_hud_display_speed(
		(float)drive->vehicle.speed, MH_UNITS_METRIC);
	state->gear = drive->vehicle.engine.gear;

	/*
	 * Binary32 RPM times binary32 .00008, stored binary32 again. Clamp the
	 * meter fraction afterwards.
	 */
	if (drive->vehicle.engine.rpm >= 12500)
		revs = 1;
	else if (!(drive->vehicle.engine.rpm > 0))
		revs = 0;
	else
		revs = (float)((double)(float)drive->vehicle.engine.rpm *
			(double).00008f);
	state->revs = revs > 1 ? 1 : revs > 0 ? revs : 0;
	state->timed = drive->race.lap_limit != 0;
	state->lap = drive->race.lap;
	state->laps = drive->race.lap_limit;
	state->total_ms = drive->race.now_ms;
	state->lap_ms = drive->race.lap_ms;
	state->last_ms = drive->race.last_ms;
	state->split_ms =
		drive->race.split_notice != 0 ? drive->race.split_ms : 0;
	state->lap_notice = drive->race.lap_notice;
	state->race_notice = drive->race.race_notice;
	state->time_notices = state->timed && drive->race.split_hold_ms != 0;
	state->best_ms = drive->race.best_lap == 0 ? 0 : drive->race.best_ms;
	state->position =
		field->standings.cars[mh_field_viewed(field)].position;
	state->cars = field->count;

	if (field->network || field->playback) {
		state->cars = 0;

		for (i = 0; i < field->count; i++)
			if (field->active_mask & (1U << i))
				state->cars++;
	}

	state->start = mh_drive_start_phase(drive);
	state->paused = paused;
	state->finished = drive->race.finished;
	state->complete = field->standings.complete;
	state->wrong_way = drive->race.wrong_way;
	state->clock_ms = clock_ms;
	state->map_mode = 2;
	state->gap_mode = 2;
	state->checkpoint_info = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
draw_sprite(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	unsigned int id, int x, int y)
{
	return mh_ui_quad(list, &assets->atlas, &assets->sprites[id], (float)x,
		(float)y, white);
}

unsigned long
mh_hud_status_flags(unsigned long flags, unsigned int mode, int playback_active,
	int recording_active)
{
	flags &= STATUS_RETAIN_MASK;

	if (playback_active && mode == MH_GAME_GHOST)
		flags |= MH_HUD_STATUS_GHOST;

	if (playback_active && mode == MH_GAME_DEMO)
		flags |= MH_HUD_STATUS_DEMO;

	if (recording_active)
		flags |= MH_HUD_STATUS_RECORDING;

	return flags;
}

int
mh_hud_status_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	int right, y;

	if (assets == NULL || state == NULL || list == NULL ||
		list->width < MH_HUD_MIN_WIDTH ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT || state->info_detail > 2)
		return 0;

	if (state->info_detail == 0 ||
		((state->clock_ms / BLINK_INTERVAL_MS) & 1UL) != 0)
		return 1;

	right = (int)list->width;
	y = (int)list->height - 20;

	if ((state->status_flags & MH_HUD_STATUS_RECORDING) &&
		!draw_sprite(assets, list, MH_HUD_CAMERA, right - 20, y))
		return 0;

	if ((state->status_flags & MH_HUD_STATUS_GHOST) &&
		!draw_sprite(assets, list, MH_HUD_GHOST, right - 36, y))
		return 0;

	/* The original Demo icon deliberately reuses the reverse-gear R. */
	if ((state->status_flags & MH_HUD_STATUS_DEMO) &&
		!draw_sprite(assets, list, MH_HUD_REVERSE, right - 36, y))
		return 0;

	return 1;
}

static int
draw_number(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	unsigned int value, int *right, int y)
{
	do {
		*right -= 20;

		if (!draw_sprite(
			assets, list, MH_HUD_SMALL + value % 10, *right, y))
			return 0;

		value /= 10;
	} while (value != 0);

	return 1;
}

static int
draw_clock(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	unsigned long ms, int right, int y)
{
	unsigned int pairs[3], i, j;

	pairs[0] = (unsigned int)((ms / 10) % 100);
	pairs[1] = (unsigned int)((ms / 1000) % 60);
	pairs[2] = (unsigned int)((ms / 60000) % 100);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 2; j++) {
			right -= 20;

			if (!draw_sprite(assets, list,
				MH_HUD_SMALL + pairs[i] % 10, right, y))
				return 0;

			pairs[i] /= 10;
		}

		if (i != 2) {
			right -= 10;

			if (!draw_sprite(assets, list, MH_HUD_COLON, right, y))
				return 0;
		}
	}

	return 1;
}

static int
draw_ratio(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	unsigned int current, unsigned int total, int right, int y)
{
	if (!draw_number(assets, list, total, &right, y))
		return 0;

	right -= 20;

	if (!draw_sprite(assets, list, MH_HUD_SLASH, right, y))
		return 0;

	return draw_number(assets, list, current, &right, y);
}

static int
draw_notice_label(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const unsigned int *ids, unsigned int count, int y)
{
	unsigned int i, width;
	int x;

	width = 3 * (count - 1);

	for (i = 0; i < count; i++) {
		if (assets->sprites[ids[i]].width > MH_HUD_VIEW_LIMIT)
			return 0;

		width += assets->sprites[ids[i]].width;
	}

	x = (int)(list->width / 2) - (int)(width / 2);

	for (i = 0; i < count; i++) {
		if (!draw_sprite(assets, list, ids[i], x, y))
			return 0;

		x += (int)assets->sprites[ids[i]].width + 3;
	}

	return 1;
}

int
mh_hud_notices_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	unsigned int ids[4], count;
	int right, y, blink;

	if (assets == NULL || state == NULL || list == NULL ||
		list->width < MH_HUD_MIN_WIDTH ||
		list->width > MH_HUD_VIEW_LIMIT ||
		assets->hardware_height > MH_HUD_FONT_LIMIT ||
		state->lap_notice > 2 || state->race_notice > 2)
		return 0;

	if (!state->time_notices)
		return 1;

	right = (int)(list->width / 2) + 70;
	blink = (state->clock_ms / BLINK_INTERVAL_MS) & 1UL;

	if ((state->lap > 1 && state->lap_ms < LAP_NOTICE_MS) ||
		state->race_notice != 0) {
		if (state->lap_notice != 2 || !blink) {
			ids[0] = state->lap_notice == 0 ? MH_HUD_LAST
							: MH_HUD_BEST;
			ids[1] = MH_HUD_LAP;
			ids[2] = MH_HUD_TIME;
			ids[3] = MH_HUD_EVER;
			count = state->lap_notice == 2 ? 4 : 3;

			if (!draw_notice_label(assets, list, ids, count, 34) ||
				!draw_clock(assets, list, state->last_ms, right,
					58))
				return 0;
		}
	} else if (state->split_ms != 0 &&
		!draw_clock(assets, list, state->split_ms, right, 34))
		return 0;

	if (state->race_notice != 0 && (state->race_notice != 2 || !blink)) {
		count = 0;

		if (state->race_notice == 2)
			ids[count++] = MH_HUD_BEST;

		ids[count++] = MH_HUD_RACE;
		ids[count++] = MH_HUD_TIME;

		if (state->race_notice == 2)
			ids[count++] = MH_HUD_EVER;

		y = 64 + (int)assets->hardware_height;

		if (!draw_notice_label(assets, list, ids, count, y) ||
			!draw_clock(
				assets, list, state->total_ms, right, y + 24))
			return 0;
	}

	return 1;
}

static int
draw_status_text(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int y)
{
	const unsigned char *p;
	unsigned int width;
	int x;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += assets->advance[*p];

	x = ((int)list->width - (int)width) / 2;

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!mh_ui_quad(list, &assets->atlas, &assets->glyphs[*p],
			(float)(x + 1), (float)(y + 1), black) ||
			!mh_ui_quad(list, &assets->atlas, &assets->glyphs[*p],
				(float)x, (float)y, white))
			return 0;

		x += assets->advance[*p];
	}

	return 1;
}

static int
draw_gauges(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	struct mh_ui_region meter;
	unsigned int value, id, fill;
	int x;

	if (state->info_detail > 1) {
		/* Draw the meter before the gear label and speed. */
		meter = assets->sprites[MH_HUD_METER];
		fill = (unsigned int)(meter.width * state->revs);
		meter.width = fill;

		if (!mh_ui_quad(list, &assets->atlas, &meter, 20, 55, white))
			return 0;

		meter = assets->sprites[MH_HUD_METER];
		meter.x += fill;
		meter.width -= fill;

		if (!mh_ui_quad(list, &assets->atlas, &meter,
			(float)(20 + fill), 55, dim) ||
			!draw_sprite(assets, list, MH_HUD_GEAR, 10, 55) ||
			!draw_sprite(assets, list,
				state->gear == 0 ? MH_HUD_REVERSE
						: MH_HUD_SMALL + state->gear,
				17 + (int)assets->sprites[MH_HUD_GEAR].width,
				56))
			return 0;
	}

	if (state->info_detail != 0) {
		if (!draw_sprite(assets, list,
			state->system == MH_UNITS_IMPERIAL ? MH_HUD_MPH
							: MH_HUD_KMH,
			100, 34))
			return 0;

		value = state->speed;
		x = 95;

		do {
			id = MH_HUD_BIG + value % 10;
			x -= 24;

			if (!draw_sprite(assets, list, id,
				x + 30 - (int)assets->sprites[id].width,
				13))
				return 0;

			value /= 10;
		} while (value != 0);
	}

	return 1;
}

static int
draw_timer_labels(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, int right, struct mh_ui_list *list)
{
	const struct mh_ui_region *sprites;
	int x, y;

	sprites = assets->sprites;

	y = 10;

	if (state->info_detail > 1) {
		x = right - (int)sprites[MH_HUD_TIME].width;

		if (!draw_sprite(assets, list, MH_HUD_TIME, x, y) ||
			!draw_sprite(assets, list, MH_HUD_TOTAL,
				x - 3 - (int)sprites[MH_HUD_TOTAL].width, y))
			return 0;

		y += 40;
	}

	/* Include TOTAL in the current-lap label. */
	x = right - (int)sprites[MH_HUD_TIME].width;

	if (!draw_sprite(assets, list, MH_HUD_TIME, x, y))
		return 0;

	x -= 3 + (int)sprites[MH_HUD_LAP].width;

	if (!draw_sprite(assets, list, MH_HUD_LAP, x, y) ||
		!draw_sprite(assets, list, MH_HUD_TOTAL,
			x - 3 - (int)sprites[MH_HUD_TOTAL].width, y))
		return 0;

	y += 40;

	if (state->info_detail > 1) {
		x = right - (int)sprites[MH_HUD_TIME].width;

		if (!draw_sprite(assets, list, MH_HUD_TIME, x, y))
			return 0;

		x -= 3 + (int)sprites[MH_HUD_LAP].width;

		if (!draw_sprite(assets, list, MH_HUD_LAP, x, y) ||
			!draw_sprite(assets, list, MH_HUD_BEST,
				x - 3 - (int)sprites[MH_HUD_BEST].width, y))
			return 0;

		y += 40;
	}

	if (!draw_sprite(assets, list, MH_HUD_LAP,
		right - (int)sprites[MH_HUD_LAP].width, y))
		return 0;

	if (state->info_detail > 1 &&
		!draw_sprite(assets, list, MH_HUD_POS,
			right - (int)sprites[MH_HUD_POS].width, y + 40))
		return 0;

	return 1;
}

static int
draw_timer_values(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, int right, struct mh_ui_list *list)
{
	int y;

	y = 30;

	if (state->info_detail > 1) {
		if (!draw_clock(assets, list, state->total_ms, right, y))
			return 0;

		y += 40;
	}

	if (!draw_clock(assets, list, state->lap_ms, right, y))
		return 0;

	y += 40;

	if (state->info_detail > 1) {
		if (!draw_clock(assets, list, state->best_ms, right, y))
			return 0;

		y += 40;
	}

	if (!draw_ratio(assets, list, state->lap, state->laps, right, y))
		return 0;

	if (state->info_detail > 1 &&
		!draw_ratio(assets, list, state->position, state->cars, right,
			y + 40))
		return 0;

	return 1;
}

static int
draw_race_timers(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, unsigned int width,
	struct mh_ui_list *list)
{
	int right;

	if (!state->timed || state->info_detail == 0)
		return 1;

	right = (int)width - 16;

	/* The original submits every label before any timer or ratio digits. */

	return draw_timer_labels(assets, state, right, list) &&
		draw_timer_values(assets, state, right, list);
}

static int
draw_hud(const struct mh_hud_assets *assets, const struct mh_hud_state *state,
	unsigned int width, unsigned int height, struct mh_ui_list *list,
	int append)
{
	unsigned int id;

	if (assets == NULL || state == NULL || list == NULL ||
		width < MH_HUD_MIN_WIDTH || height < MH_HUD_MIN_HEIGHT ||
		width > MH_HUD_VIEW_LIMIT || height > MH_HUD_VIEW_LIMIT ||
		!(state->revs >= 0 && state->revs <= 1) ||
		state->speed > SPEED_LIMIT ||
		state->system > MH_UNITS_IMPERIAL || state->gear > 9 ||
		state->laps > 99 || state->lap > 99 || state->cars > 99 ||
		state->position > 99 || state->info_detail > 2 ||
		state->checkpoint_info > 2)
		return 0;

	if (append) {
		if (list->width != width || list->height != height ||
			list->count > MH_UI_MAX_QUADS * 6)
			return 0;
	} else
		mh_ui_begin(list, width, height);

	if (state->show_info)
		return mh_hud_gaps_draw(assets, state, list) &&
			mh_race_info_draw(assets, &state->info, list) &&
			mh_hud_messages_draw(assets, state, list) &&
			mh_chat_draw(
				assets, state->chat_text, state->chat_ms, list);

	if (!draw_gauges(assets, state, list) ||
		!draw_race_timers(assets, state, width, list))
		return 0;

	if (!mh_hud_secrets_draw(assets, &state->secrets, list) ||
		!mh_hud_map_draw(assets, state, list))
		return 0;

	if (state->info_detail > 1 && state->wrong_way &&
		((state->clock_ms / BLINK_INTERVAL_MS) & 1UL) == 0 &&
		!draw_sprite(assets, list, MH_HUD_WRONG_WAY,
			((int)width -
				(int)assets->sprites[MH_HUD_WRONG_WAY].width) /
				2,
			10))
		return 0;

	if (state->info_detail != 0 && state->checkpoint_info != 0 &&
		!mh_hud_notices_draw(assets, state, list))
		return 0;

	if (!mh_hud_status_draw(assets, state, list) ||
		!mh_hud_gaps_draw(assets, state, list) ||
		!mh_hud_messages_draw(assets, state, list) ||
		!mh_chat_draw(assets, state->chat_text, state->chat_ms, list))
		return 0;

	if (state->message != NULL)
		return draw_status_text(
			assets, list, state->message, (int)height / 2);

	if (state->paused)
		return draw_status_text(
			assets, list, "PAUSED", (int)height / 2);

	if (state->finished)
		return draw_status_text(assets, list,
			state->complete ? "RESULTS - SHIFT+TAB"
					: "FINISHED - SHIFT+TAB",
			(int)height / 2);

	if (state->start >= MH_START_THREE && state->start <= MH_START_GO) {
		id = MH_HUD_THREE + (unsigned int)state->start - MH_START_THREE;
		return draw_sprite(assets, list, id,
			(int)(width / 2 - assets->sprites[id].width / 2),
			(int)height / 8);
	}

	if (state->start == MH_START_READY)
		return draw_status_text(assets, list, "READY", (int)height / 2);

	return 1;
}

int
mh_hud_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, unsigned int width,
	unsigned int height, struct mh_ui_list *list)
{
	return draw_hud(assets, state, width, height, list, 0);
}

int
mh_hud_append(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, unsigned int width,
	unsigned int height, struct mh_ui_list *list)
{
	unsigned int saved_count;
	int result;

	if (list == NULL)
		return 0;

	saved_count = list->count;
	result = draw_hud(assets, state, width, height, list, 1);

	if (!result)
		list->count = saved_count;

	return result;
}

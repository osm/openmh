#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/ui/lobby_roster.h"

enum {
	ROSTER_ROW_SPACING = 12,
	PATCHED_ROW_SPACING_MIN = 10,
	PATCHED_TABLE_TOP = 165,
	PATCHED_TABLE_BOTTOM = 285
};

static unsigned int
patched_spacing(unsigned int rows)
{
	unsigned int spacing;

	if (rows == 0)
		return ROSTER_ROW_SPACING;

	/* Use normal text spacing unless a full roster needs compact rows. */
	spacing = (PATCHED_TABLE_BOTTOM - PATCHED_TABLE_TOP) / rows;

	if (spacing > ROSTER_ROW_SPACING)
		spacing = ROSTER_ROW_SPACING;

	if (spacing < PATCHED_ROW_SPACING_MIN)
		spacing = PATCHED_ROW_SPACING_MIN;

	return spacing;
}

static int
valid_word(unsigned long value)
{
	return value <= MH_U32_MAX;
}

static int
valid_roster(const struct mh_lobby_roster *roster)
{
	const struct mh_lobby_roster_player *player;
	unsigned int i;

	if (roster == NULL || roster->count == 0 ||
		roster->count > MH_LOBBY_ROSTER_PLAYERS ||
		roster->player >= roster->count ||
		(roster->layout != MH_LOBBY_ROSTER_F12 &&
			roster->layout != MH_LOBBY_ROSTER_31) ||
		!valid_word(roster->password_mode) ||
		!valid_word(roster->mode) || !valid_word(roster->laps) ||
		memchr(roster->session, 0, 17) == NULL ||
		memchr(roster->password, 0, 17) == NULL ||
		memchr(roster->track, 0, 17) == NULL)
		return 0;

	for (i = 0; i < roster->count; i++) {
		player = &roster->players[i];

		if (memchr(player->name, 0, 17) == NULL ||
			memchr(player->team, 0, 5) == NULL ||
			memchr(player->car, 0, 17) == NULL ||
			!valid_word(player->racing) ||
			!valid_word(player->lobby) ||
			!valid_word(player->recording) ||
			!valid_word(player->ready) ||
			!valid_word(player->pass) ||
			!valid_word(player->gear) ||
			!valid_word(player->color) ||
			!valid_word(player->races) ||
			!valid_word(player->points) ||
			!valid_word(player->position) ||
			!valid_word(player->lap) || !valid_word(player->ping))
			return 0;
	}

	return 1;
}

static int
valid_statistics(const struct mh_lobby_roster *roster)
{
	const struct mh_net_statistics_row *row;
	unsigned int i;

	if (roster->statistics == NULL)
		return 1;

	if (roster->layout != MH_LOBBY_ROSTER_31 ||
		roster->system > MH_UNITS_IMPERIAL)
		return 0;

	for (i = 0; i + 1 < roster->count; i++) {
		row = &roster->statistics->rows[i];

		if (row->position > 255 || row->top_speed_kmh > MH_U16_MAX ||
			!valid_word(row->color) ||
			!valid_word(row->best_lap_ms) ||
			!valid_word(row->race_ms))
			return 0;
	}

	return 1;
}

static unsigned int
palette_index(
	const unsigned char *palette, unsigned long rgb, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (palette[i * 3] == ((rgb >> 16) & 255) &&
			palette[i * 3 + 1] == ((rgb >> 8) & 255) &&
			palette[i * 3 + 2] == (rgb & 255))
			return i;

	return 0;
}

static int
same_team(const char *a, const char *b)
{
	return mh_ascii_equal_n(a, b, 4);
}

static void
append_line(struct mh_lobby_roster_view *view, unsigned int x, unsigned int y,
	unsigned int ink, const char *text)
{
	struct mh_lobby_roster_line *line;

	line = &view->lines[view->count++];
	line->x = x;
	line->y = y;
	line->color = ink;
	strcpy((char *)line->text, text);
}

static void
append_trimmed(struct mh_lobby_roster_view *view, unsigned int x,
	unsigned int y, unsigned int ink, const char *text, unsigned int limit,
	const unsigned int advance[MH_FONT_GLYPHS])
{
	char trimmed[17];
	unsigned int i, length, width;

	strcpy(trimmed, text);
	length = (unsigned int)strlen(trimmed);
	width = 0;

	for (i = 0; i < length; i++)
		width += advance[(unsigned char)trimmed[i]];

	while (length && width > limit) {
		length--;
		width -= advance[(unsigned char)trimmed[length]];
		trimmed[length] = 0;
	}

	append_line(view, x, y, ink, trimmed);
}

static void
append_number(struct mh_lobby_roster_view *view, unsigned int x, unsigned int y,
	unsigned int ink, unsigned long value)
{
	char text[16], reverse[16];
	unsigned int length, count, digit;
	unsigned long magnitude;

	/*
	 * Negate INT_MIN in 32 bits, then divide as signed. That one value
	 * prints punctuation. Preserve it without C signed overflow.
	 */
	magnitude = value > (unsigned long)MH_I32_MAX ? (MH_U32_MAX - value) + 1
						: value;
	count = length = 0;

	do {
		digit = (unsigned int)(magnitude % 10);
		magnitude /= 10;
		reverse[count++] =
			(char)(value == MH_U32_SIGN_BIT ? '0' - digit
							: '0' + digit);
	} while (magnitude);

	if (value > (unsigned long)MH_I32_MAX)
		text[length++] = '-';

	while (count)
		text[length++] = reverse[--count];

	text[length] = 0;
	append_line(view, x, y, ink, text);
}

static long
signed_word(unsigned long value)
{
	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

static void
append_player(const struct mh_lobby_roster_player *player, unsigned int y,
	unsigned int racing, unsigned int ink,
	enum mh_lobby_roster_layout layout,
	const unsigned int advance[MH_FONT_GLYPHS], unsigned long team_points,
	struct mh_lobby_roster_view *view)
{
	char score[32];

	if (layout == MH_LOBBY_ROSTER_31) {
		if (racing) {
			sprintf(score, "%lu.", player->position);
			append_line(view, 30, y, ink, score);
		} else
			append_line(view, 30, y, ink, "L.");

		if (player->ready || player->pass)
			append_line(view, 50, y + 2, ink,
				player->ready ? "R" : "P");
	} else {
		if (racing)
			append_number(view, 30, y, ink, player->position);
		else
			append_line(view, 30, y, ink, "L");
		append_line(view, 40, y, ink, ".");

		if (player->recording)
			append_line(view, 50, y + 5, ink, "R");
	}

	append_trimmed(view, 75, y, ink, player->name, 140, advance);
	append_trimmed(view, 220, y, ink, player->car, 80, advance);
	append_line(view, 305, y, ink, player->gear ? "A" : "M");
	append_line(view, 340, y, ink, player->team);
	append_number(view, 400, y, ink, player->races);

	/* These two fields use CRT sprintf, unlike the number painter above. */
	if (player->team[0])
		sprintf(score,
			layout == MH_LOBBY_ROSTER_31 ? "%ld[%ld]" : "%ld-%ld",
			signed_word(player->points), signed_word(team_points));
	else
		sprintf(score, "%ld", signed_word(player->points));
	append_line(view, 460, y, ink, score);

	if (racing)
		append_number(view, 540, y, ink, player->lap);

	append_number(view, 580, y, ink, player->ping);
}

static void
append_time(struct mh_lobby_roster_view *view, unsigned int x, unsigned int y,
	unsigned int ink, unsigned long milliseconds)
{
	char text[32];

	sprintf(text, "%lu:%02lu:%02lu", milliseconds / 60000,
		(milliseconds / 1000) % 60, (milliseconds / 10) % 100);
	append_line(view, x, y, ink, text);
}

static void
append_statistics(const struct mh_lobby_roster *roster,
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_lobby_roster_view *view)
{
	/* No HUD speed clamp applies to this table. */
	static const float factors[] = {1.0f, 0.6213712096214294f};
	static const char *units[] = {"km/h", "mph"};
	const struct mh_net_statistics_row *row;
	unsigned int i, y, ink, speed, rows, spacing;
	char text[32], name[MH_NET_NAME_BYTES + 1];

	rows = 0;

	for (i = 0; i + 1 < roster->count; i++)
		rows += roster->statistics->rows[i].position != 0;

	spacing = patched_spacing(rows);
	y = PATCHED_TABLE_TOP;

	for (i = 0; i + 1 < roster->count; i++) {
		row = &roster->statistics->rows[i];

		if (row->position == 0)
			continue;

		ink = palette_index(palette, row->color, 256);
		sprintf(text, "%u.", row->position);
		append_line(view, 30, y, ink, text);
		memcpy(name, row->name, MH_NET_NAME_BYTES);
		name[MH_NET_NAME_BYTES] = 0;
		append_trimmed(view, 75, y, ink, name, 140, advance);
		append_time(view, 220, y, ink, row->best_lap_ms);
		append_time(view, 365, y, ink, row->race_ms);
		speed = (unsigned int)((long double)row->top_speed_kmh *
			factors[roster->system]);
		sprintf(text, "%u %s", speed, units[roster->system]);
		append_line(view, 487, y, ink, text);
		y += spacing;
	}
}

static void
append_settings(const struct mh_lobby_roster *roster,
	const unsigned int advance[MH_FONT_GLYPHS], unsigned int orange,
	struct mh_lobby_roster_view *view)
{
	const struct mh_lobby_roster_player *player;

	if (!roster->password_mode &&
		(roster->layout == MH_LOBBY_ROSTER_F12 ||
			roster->players[roster->player].lobby)) {
		append_line(view, 170, 28, orange, roster->session);
		append_line(view, 170, 40, orange, roster->password);
		append_line(view, 170, 52, orange, roster->track);
		append_number(view, 170, 64, orange, roster->laps);
		append_line(view, 170, 76, orange,
			roster->mode ? "Elimination" : "Normal");
		player = &roster->players[roster->player];

		if (player->lobby) {
			append_trimmed(view, 450, 28, orange, player->name, 175,
				advance);
			append_line(view, 450, 40, orange, player->team);
			append_line(view, 450, 52, orange,
				player->gear ? "Automatic" : "Manual");
			append_line(view, 450, 64, orange, player->car);
		}
	}
}

static void
sum_team_points(
	const struct mh_lobby_roster *roster, struct mh_lobby_roster_view *view)
{
	const struct mh_lobby_roster_player *player;
	unsigned int i, j;

	for (i = 0; i < roster->count; i++) {
		player = &roster->players[i];
		view->team_points[i] = player->points;

		if (!player->lobby || !player->team[0])
			continue;

		for (j = 0; j < roster->count; j++)
			if (i != j && roster->players[j].lobby &&
				same_team(
					player->team, roster->players[j].team))
				view->team_points[i] =
					(view->team_points[i] +
						roster->players[j].points) &
					MH_U32_MAX;
	}
}

static unsigned int
order_lobby_players(const struct mh_lobby_roster *roster,
	unsigned int order[MH_LOBBY_ROSTER_PLAYERS])
{
	unsigned int i, j, count, slot;

	count = 0;

	for (i = 0; i < roster->count; i++)
		if (roster->players[i].lobby) {
			j = count;
			slot = i;

			while (j &&
				roster->players[order[j - 1]].points <
					roster->players[slot].points) {
				order[j] = order[j - 1];
				j--;
			}

			order[j] = slot;
			count++;
		}

	return count;
}

enum mh_error_code
mh_lobby_roster_view(const struct mh_lobby_roster *roster,
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_lobby_roster_view *view, struct mh_error *error_out)
{
	struct mh_lobby_roster_view result;
	const struct mh_lobby_roster_player *player;
	unsigned int i, rank, lobby_count, row_count,
		order[MH_LOBBY_ROSTER_PLAYERS], slot, orange, palette_count,
		row_spacing, race_y, lobby_y;

	if (!valid_roster(roster) || !valid_statistics(roster) ||
		advance == NULL || palette == NULL || view == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby roster view");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby font width");

	memset(&result, 0, sizeof(result));
	palette_count = roster->layout == MH_LOBBY_ROSTER_31 ? 256 : 255;
	row_count = 0;

	for (i = 0; i < roster->count; i++) {
		row_count += roster->players[i].racing != 0;
		row_count += roster->players[i].lobby != 0;
	}

	row_spacing = roster->layout == MH_LOBBY_ROSTER_31
		? patched_spacing(row_count)
		: ROSTER_ROW_SPACING;

	race_y = roster->layout == MH_LOBBY_ROSTER_31 ? 160 : 170;
	lobby_y =
		roster->layout == MH_LOBBY_ROSTER_31 ? PATCHED_TABLE_TOP : 180;
	orange = palette_index(palette, 0xe06820UL, palette_count);

	append_settings(roster, advance, orange, &result);

	sum_team_points(roster, &result);

	if (roster->statistics != NULL) {
		append_statistics(roster, advance, palette, &result);
		*view = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	row_count = 0;

	for (rank = 1; rank <= roster->count; rank++) {
		for (i = 0; i < roster->count; i++)
			if (roster->players[i].racing &&
				roster->players[i].position == rank)
				break;

		if (i == roster->count)
			continue;

		player = &roster->players[i];
		append_player(player, race_y + row_spacing * row_count, 1,
			palette_index(palette, player->color, palette_count),
			roster->layout, advance, result.team_points[i],
			&result);
		row_count++;
	}

	lobby_count = order_lobby_players(roster, order);

	for (i = 0; i < lobby_count; i++) {
		slot = order[i];
		player = &roster->players[slot];
		append_player(player, lobby_y + row_spacing * row_count, 0,
			palette_index(palette, player->color, palette_count),
			roster->layout, advance, result.team_points[slot],
			&result);
		row_count++;
	}

	*view = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_roster_draw(const struct mh_lobby_roster *roster,
	const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_lobby_roster_view view;
	enum mh_error_code status;
	unsigned int i;

	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, 0, 0, 0, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_lobby_roster_view(
		roster, font->advance, palette, &view, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Font/canvas validation plus bounded generated text/coordinates covers
	 * every request before any pixel is touched.
	 */
	for (i = 0; i < view.count; i++)
		mh_menu_text_draw(font, view.lines[i].text, 32, view.lines[i].x,
			view.lines[i].y, view.lines[i].color, canvas, NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

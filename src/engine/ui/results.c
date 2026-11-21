#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/ui/results.h"

#define BOARD_WIDTH 640
#define BOARD_HEIGHT 400
#define MIN_WIDTH 320
#define MIN_HEIGHT 200
#define MAX_DIMENSION 16384

static const float white[4] = {1, 1, 1, 1};
static const float ink[4] = {248.0f / 255, 248.0f / 255, 248.0f / 255, 1};

static void
format_time(char *out, char prefix, unsigned long ms)
{
	sprintf(out, "%c%lu:%02lu,%02lu", prefix, ms / 60000, (ms / 1000) % 60,
		(ms / 10) % 100);
}

void
mh_results_row(const struct mh_race_result *result, struct mh_results_row *row)
{
	static const unsigned int points[] = {12, 10, 8, 6, 4, 3, 2, 1};

	char prefix;
	unsigned long milliseconds;

	strcpy(row->score, "--");
	strcpy(row->race, "Racing");
	strcpy(row->best, " --");

	if (result->finished || result->estimated) {
		if (result->position >= 1 &&
			result->position <= sizeof(points) / sizeof(points[0]))
			sprintf(row->score, "%u", points[result->position - 1]);

		if (result->position == 1) {
			prefix = ' ';
			milliseconds = result->finish_ms;
		} else if (result->gap_ms < 0) {
			prefix = '-';
			milliseconds = 0UL - (unsigned long)result->gap_ms;
		} else {
			prefix = '+';
			milliseconds = (unsigned long)result->gap_ms;
		}

		format_time(row->race, prefix, milliseconds);
	}

	if (result->estimated || result->best_lap != 0)
		format_time(row->best, ' ', result->best_ms);
}

static int
draw_text(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, struct mh_ui_list *list,
	const char *text, unsigned int x, unsigned int y, unsigned int right)
{
	const unsigned char *p;

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (x >= right || assets->advance[*p] > right - x)
			break;

		if (!mh_ui_quad(list, atlas, &assets->glyphs[*p], (float)x,
			(float)y, ink))
			return 0;

		x += assets->advance[*p];
	}

	return 1;
}

static void
scale_board(struct mh_ui_list *list, unsigned int width, unsigned int height)
{
	float scale, dx, dy;
	unsigned int i;

	scale = (float)width / BOARD_WIDTH;

	if ((float)height / BOARD_HEIGHT < scale)
		scale = (float)height / BOARD_HEIGHT;

	dx = (width - BOARD_WIDTH * scale) / 2;
	dy = (height - BOARD_HEIGHT * scale) / 2;

	for (i = 0; i < list->count; i++) {
		list->vertices[i].position[0] =
			dx + scale * list->vertices[i].position[0];
		list->vertices[i].position[1] =
			dy + scale * list->vertices[i].position[1];
	}

	list->width = width;
	list->height = height;
}

/* Native overflow layout: retain every recorded entrant on one board. */
static int
draw_large_roster(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const struct mh_standings *standings,
	struct mh_ui_list *list)
{
	struct mh_results_row row;
	unsigned int i, entry, y;
	char rank[16];

	if (!draw_text(assets, atlas, list, "Pos", 20, 16, 70) ||
		!draw_text(assets, atlas, list, "Driver", 80, 16, 290) ||
		!draw_text(assets, atlas, list, "Race", 300, 16, 460) ||
		!draw_text(assets, atlas, list, "Best Lap", 470, 16, 630))
		return 0;

	for (i = 0; i < standings->count; i++) {
		entry = standings->order[i];
		y = 40 + i * 24;
		mh_results_row(&standings->cars[entry], &row);
		sprintf(rank, "%u", standings->cars[entry].position);

		if (assets->left_mask & (1U << entry))
			strcpy(row.race, "Left race");

		if (!draw_text(assets, atlas, list, rank, 20, y, 70) ||
			!draw_text(assets, atlas, list,
				assets->profiles[entry].nick, 80, y, 290) ||
			!draw_text(
				assets, atlas, list, row.race, 300, y, 460) ||
			!draw_text(assets, atlas, list, row.best, 470, y, 630))
			return 0;
	}

	return 1;
}

static int
draw_race_row(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const struct mh_standings *standings,
	unsigned int row_index, struct mh_ui_list *list)
{
	struct mh_results_row row;
	unsigned int entry, y, label, value, right;

	entry = standings->order[row_index];
	y = (row_index / 2) * 80 + (row_index % 2) * 40;
	label = row_index % 2 ? 430 : 125;
	value = row_index % 2 ? 510 : 210;
	right = row_index % 2 ? 635 : 315;
	mh_results_row(&standings->cars[entry], &row);

	if (assets->left_mask & (1U << entry)) {
		strcpy(row.race, "Left race");
		strcpy(row.score, "--");
	}

	return mh_ui_quad(list, atlas, &assets->portraits[entry],
		row_index % 2 ? 327.0f : 20.0f, (float)(22 + y),
		white) &&
		draw_text(assets, atlas, list, assets->profiles[entry].name,
			row_index % 2 ? 375 : 60, 85 + y, right) &&
		draw_text(assets, atlas, list, "Nick:", label, 30 + y, right) &&
		draw_text(
			assets, atlas, list, "Score:", label, 40 + y, right) &&
		draw_text(assets, atlas, list, "Race:", label, 50 + y, right) &&
		draw_text(assets, atlas, list, "Best Lap:", label, 60 + y,
			right) &&
		draw_text(assets, atlas, list, assets->profiles[entry].nick,
			value + assets->advance['+'], 30 + y, right) &&
		draw_text(assets, atlas, list, row.score,
			value + assets->advance['+'], 40 + y, right) &&
		draw_text(
			assets, atlas, list, row.race, value, 50 + y, right) &&
		draw_text(assets, atlas, list, row.best, value, 60 + y, right);
}

int
mh_results_draw(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const struct mh_standings *standings,
	unsigned int width, unsigned int height, const char *footer,
	struct mh_ui_list *list)
{
	unsigned int i, entry, seen;

	if (assets == NULL || atlas == NULL || standings == NULL ||
		list == NULL || standings->count == 0 ||
		standings->count > MH_RESULTS_MAX_PLAYERS ||
		assets->count != standings->count || width < MIN_WIDTH ||
		height < MIN_HEIGHT || width > MAX_DIMENSION ||
		height > MAX_DIMENSION)
		return 0;

	seen = 0;

	for (i = 0; i < standings->count; i++) {
		entry = standings->order[i];

		if (entry >= standings->count || (seen & (1U << entry)) != 0)
			return 0;

		seen |= 1U << entry;
	}

	mh_ui_begin(list, BOARD_WIDTH, BOARD_HEIGHT);

	if (!mh_ui_quad_scaled(list, atlas, &assets->background, 0, 0,
		BOARD_WIDTH, BOARD_HEIGHT, white))
		return 0;

	if (standings->count > MH_RACE_LEGACY_CARS) {
		if (!draw_large_roster(assets, atlas, standings, list))
			return 0;
	} else
		for (i = 0; i < standings->count; i++) {
			if (!draw_race_row(assets, atlas, standings, i, list))
				return 0;
		}

	if (footer != NULL &&
		!draw_text(assets, atlas, list, footer, 10, 386, 630))
		return 0;

	scale_board(list, width, height);

	return 1;
}

static int
draw_season_row(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, unsigned long points,
	unsigned int row_index, struct mh_ui_list *list)
{
	unsigned int y, label, value, right;
	long score;
	char number[32];

	y = row_index * 40;
	label = row_index % 2 ? 430 : 125;
	value = row_index % 2 ? 500 : 200;
	right = row_index % 2 ? 635 : 315;
	score = points <= (unsigned long)MH_I32_MAX
		? (long)points
		: -1L - (long)(MH_U32_MAX - points);
	sprintf(number, "%ld", score);

	return mh_ui_quad(list, atlas, &assets->portraits[row_index],
		row_index % 2 ? 327.0f : 20.0f, (float)(22 + y),
		white) &&
		draw_text(assets, atlas, list, assets->profiles[row_index].name,
			row_index % 2 ? 375 : 60, 85 + y, right) &&
		draw_text(assets, atlas, list, "Nick:", label, 30 + y, right) &&
		draw_text(assets, atlas, list, assets->profiles[row_index].nick,
			value, 30 + y, right) &&
		draw_text(
			assets, atlas, list, "Score:", label, 40 + y, right) &&
		draw_text(assets, atlas, list, number, value, 40 + y, right);
}

int
mh_results_season_draw(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	unsigned int width, unsigned int height, const char *footer,
	struct mh_ui_list *list)
{
	unsigned int i;

	if (assets == NULL || atlas == NULL || points == NULL || list == NULL ||
		(assets->count != 2 && assets->count != 8) ||
		width < MIN_WIDTH || height < MIN_HEIGHT ||
		width > MAX_DIMENSION || height > MAX_DIMENSION)
		return 0;

	for (i = 0; i < assets->count; i++)
		if (points[i] > MH_U32_MAX ||
			memchr(assets->profiles[i].name, 0,
				sizeof(assets->profiles[i].name)) == NULL ||
			memchr(assets->profiles[i].nick, 0,
				sizeof(assets->profiles[i].nick)) == NULL)
			return 0;

	mh_ui_begin(list, BOARD_WIDTH, BOARD_HEIGHT);

	if (!mh_ui_quad(list, atlas, &assets->background, 0, 0, white))
		return 0;

	for (i = 0; i < assets->count; i++) {
		if (!draw_season_row(assets, atlas, points[i], i, list))
			return 0;
	}

	if (footer != NULL &&
		!draw_text(assets, atlas, list, footer, 10, 386, 630))
		return 0;

	scale_board(list, width, height);

	return 1;
}

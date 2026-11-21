#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league_tracks.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_single.h"

#define LAST_CHOICE 4
#define LAST_DIFFICULTY 2
#define CATALOG_LIMIT 4096
#define STAGE_LIMIT 3
#define DIVISION_BASE 100
#define SPECIAL_REMAINDER 10
#define LEAGUE_DIVISIONS 4
#define SUMMARY_LAYOUT 10
#define SUMMARY_ORIGIN_SLOT 10
#define SUMMARY_OFFSET_SLOT 20
#define SUMMARY_SPACING_SLOT 21
#define SUMMARY_VALUES_SLOT 22

static void
append_event(
	struct mh_menu_output *result, unsigned int kind, unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

enum mh_error_code
mh_menu_single_update(struct mh_menu_single_state *state,
	unsigned long demo_count, const struct mh_menu_input *input,
	struct mh_menu_output *output, const struct mh_menu_choices *choices,
	struct mh_error *error_out)
{
	static const unsigned int destinations[LAST_CHOICE + 1] = {
		MH_MENU_QUICK, MH_MENU_RACE_SETUP, MH_MENU_LEAGUE_SETUP,
		MH_MENU_RACE_SETUP, MH_MENU_GHOST_SETUP};

	struct mh_menu_single_state next;
	struct mh_menu_output result;

	if (state == NULL || input == NULL || output == NULL ||
		state->selected > LAST_CHOICE || state->mode > MH_GAME_GHOST ||
		demo_count > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player menu input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (next.mode == MH_GAME_GHOST && demo_count == 0)
		next.mode = MH_GAME_TIME_ATTACK;

	next.selected = mh_menu_choice_first(choices, next.selected);

	if (input->left == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(
				choices, &next.selected, LAST_CHOICE + 1, 0));

	if (input->right == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(
				choices, &next.selected, LAST_CHOICE + 1, 1));

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, next.selected) != MH_MENU_ENABLED)
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_LIMIT);

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, next.selected) ==
			MH_MENU_ENABLED) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
		append_event(
			&result, MH_MENU_PAGE, destinations[next.selected]);
	}

	if (input->escape == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_MAIN);
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_single_leave(
	struct mh_menu_single_state *state, struct mh_error *error_out)
{
	if (state == NULL || state->selected > LAST_CHOICE ||
		state->mode > MH_GAME_GHOST)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player departure");

	if (state->selected != 0)
		state->mode = state->selected - 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_name(const char *text)
{
	unsigned int i;

	if (text == NULL)
		return 0;

	for (i = 0; i < MH_MENU_SINGLE_NAME_BYTES; i++)
		if (text[i] == 0)
			return 1;

	return 0;
}

static void
append_label(struct mh_menu_single_summary *result, long x, long y,
	unsigned int color, const char *text)
{
	struct mh_menu_single_label *next;

	next = &result->labels[result->count++];
	next->x = x;
	next->y = y;
	next->color = color;
	strcpy(next->text, text);
}

static void
clamp_league_car(struct mh_menu_single_state *state,
	const struct mh_menu_single_details *details)
{
	unsigned int i, ordinary, eligible;

	ordinary = eligible = 0;

	for (i = 0; i < details->car_count; i++)
		if (details->cars[i].division % DIVISION_BASE <
			SPECIAL_REMAINDER)
			ordinary++;

	/*
	 * Count ordinary cars, then check that many leading entries. Do not
	 * build a filtered list.
	 */
	for (i = 0; i < ordinary; i++)
		if (details->cars[i].division / DIVISION_BASE <=
			LEAGUE_DIVISIONS - details->stage)
			eligible++;

	if (eligible <= state->car)
		state->car = eligible != 0 ? eligible - 1 : 0;
}

static enum mh_error_code
track_name(const struct mh_menu_single_details *details, unsigned int mode,
	const char **name, struct mh_error *error_out)
{
	unsigned int i, track, eligible;

	track = details->track;
	eligible = 0;

	if (mode == MH_GAME_LEAGUE) {
		track = details->track_count;

		for (i = 0; i < details->track_count; i++)
			if (mh_league_track_eligible(
				details->tracks[i].division, details->stage,
				0)) {
				if (eligible == details->race_index) {
					track = i;
					break;
				}

				eligible++;
			}
	}

	if (track >= details->track_count) {
		/*
		 * Native recovery for a saved final race whose season
		 * transition was interrupted. Continue League resumes
		 * that transition.
		 */
		if (mode != MH_GAME_LEAGUE || eligible == 0 ||
			details->race_index != eligible)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"single-player information track is unavailable");

		*name = "Season complete";
	} else
		*name = details->tracks[track].name;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_single_summary(const struct mh_menu_layout *layout, unsigned int mix,
	struct mh_menu_single_state *state,
	const struct mh_menu_single_details *details,
	struct mh_menu_single_summary *output, struct mh_error *error_out)
{
	static const char *const modes[4] = {
		"Single race", "League race", "Time attack", "Ghostmode"};

	static const char *const difficulty[3] = {"Easy", "Medium", "Hard"};
	struct mh_menu_single_summary result;
	struct mh_menu_single_state next;
	enum mh_error_code status;
	const char *name = NULL;
	char number[16];
	unsigned int i;
	long x, y, dx, dy, column, row, left_value, right_value;

	if (state == NULL || details == NULL || output == NULL ||
		state->selected > LAST_CHOICE || state->mode > MH_GAME_GHOST ||
		details->cars == NULL || details->car_count == 0 ||
		details->car_count > CATALOG_LIMIT || details->tracks == NULL ||
		details->track_count == 0 ||
		details->track_count > CATALOG_LIMIT ||
		state->car >= details->car_count ||
		details->stage > STAGE_LIMIT ||
		details->difficulty > LAST_DIFFICULTY ||
		details->automatic > 1 || details->laps > MH_RACE_MAX_LAPS ||
		details->laps < MH_RACE_MIN_LAPS ||
		!valid_name(details->demo_name))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player information");

	for (i = 0; i < details->car_count; i++)
		if (!valid_name(details->cars[i].name) ||
			details->cars[i].division > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu car catalog");

	for (i = 0; i < details->track_count; i++)
		if (!valid_name(details->tracks[i].name) ||
			details->tracks[i].division > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu track catalog");

	status = mh_menu_position(layout, SUMMARY_LAYOUT, SUMMARY_ORIGIN_SLOT,
		mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, SUMMARY_LAYOUT,
			SUMMARY_OFFSET_SLOT, mix, &dx, &dy, error_out);

	if (status == MH_ERROR_NONE) {
		x += dx;
		y += dy;
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, SUMMARY_LAYOUT,
			SUMMARY_SPACING_SLOT, mix, &column, &row, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, SUMMARY_LAYOUT,
			SUMMARY_VALUES_SLOT, mix, &left_value, &right_value,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	memset(&result, 0, sizeof(result));
	append_label(&result, x, y, 3, "Mode:");
	append_label(&result, x, y + row, 3, "Car:");
	append_label(&result, x, y + 2 * row, 3,
		next.mode == MH_GAME_LEAGUE	     ? "Division:"
			: next.mode == MH_GAME_GHOST ? "Name:"
						: "Laps:");
	append_label(&result, x + column, y, 3, "Difficulty:");
	append_label(&result, x + column, y + row, 3, "Transmission:");

	if (next.mode != MH_GAME_GHOST)
		append_label(&result, x + column, y + 2 * row, 3, "Track:");

	append_label(&result, x + left_value, y, 1, modes[next.mode]);

	if (next.mode == MH_GAME_LEAGUE) {
		clamp_league_car(&next, details);
	}

	append_label(&result, x + left_value, y + row, 1,
		details->cars[next.car].name);

	if (next.mode == MH_GAME_GHOST)
		append_label(&result, x + left_value, y + 2 * row, 1,
			details->demo_name);
	else {
		sprintf(number, "%u",
			next.mode == MH_GAME_LEAGUE ? details->stage
						: details->laps);
		append_label(&result, x + left_value, y + 2 * row, 1, number);
	}

	/*
	 * Slot 22's Y is intentionally used as the right value-column X offset.
	 */
	append_label(&result, x + column + right_value, y, 1,
		next.mode == MH_GAME_LEAGUE || next.mode == MH_GAME_GHOST
			? "-"
			: difficulty[details->difficulty]);
	append_label(&result, x + column + right_value, y + row, 1,
		details->automatic ? "Auto" : "Manual");

	if (next.mode != MH_GAME_GHOST) {
		status = track_name(details, next.mode, &name, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		append_label(&result, x + column + right_value, y + 2 * row, 1,
			name);
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

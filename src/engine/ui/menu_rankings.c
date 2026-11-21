#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/lap_presets.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_rankings.h"

static int
valid_state(const struct mh_menu_rankings_state *state, unsigned int count)
{
	return state != NULL && count != 0 &&
		count <= MH_MENU_RANKINGS_TRACK_LIMIT &&
		state->focus <= MH_MENU_RANKINGS_RECORD_CLASS &&
		state->category <= MH_HIGHSCORE_LEAGUE &&
		state->track < count &&
		state->division < MH_MENU_RANKINGS_DIVISIONS &&
		state->lap_choice < MH_SINGLE_LAP_PRESET_COUNT &&
		state->difficulty < MH_MENU_RANKINGS_DIFFICULTIES &&
		state->record_class <= 1;
}

void
mh_menu_rankings_defaults(struct mh_menu_rankings_state *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->category = MH_HIGHSCORE_LEAGUE;
	state->difficulty = state->record_class = 1;
}

enum mh_error_code
mh_menu_rankings_init(struct mh_menu_rankings_state *state, unsigned int mode,
	unsigned int track, unsigned long requested_laps,
	unsigned int difficulty, unsigned int record_class,
	unsigned int track_count, struct mh_error *error_out)
{
	struct mh_menu_rankings_state next;

	if (!valid_state(state, track_count) || mode > MH_GAME_DEMO ||
		record_class > 1 || requested_laps > MH_U32_MAX ||
		(mode != MH_GAME_LEAGUE &&
			(track >= track_count ||
				difficulty >= MH_MENU_RANKINGS_DIFFICULTIES)))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Rankings entry");

	next = *state;

	if (mode != MH_GAME_LEAGUE) {
		next.track = track;
		next.difficulty = difficulty;
	}

	next.category = mode <= MH_GAME_LEAGUE ? MH_HIGHSCORE_SINGLE
					: MH_HIGHSCORE_RACE;
	next.lap_choice = 0;

	while (next.lap_choice < MH_SINGLE_LAP_PRESET_COUNT - 1 &&
		requested_laps > mh_single_lap_presets[next.lap_choice])
		next.lap_choice++;

	next.record_class = record_class;
	*state = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
append_event(struct mh_menu_rankings_output *result, unsigned int kind,
	unsigned int value)
{
	result->events.events[result->events.count].kind = kind;

	result->events.events[result->events.count++].value = value;
}

static unsigned int *
selected_field(struct mh_menu_rankings_state *state, unsigned int tracks,
	unsigned int *count)
{
	if (state->focus == MH_MENU_RANKINGS_CATEGORY) {
		*count = MH_MENU_RANKINGS_CATEGORIES;
		return &state->category;
	}

	if (state->focus == MH_MENU_RANKINGS_TRACK) {
		if (state->category == MH_HIGHSCORE_LEAGUE) {
			*count = MH_MENU_RANKINGS_DIVISIONS;
			return &state->division;
		}

		*count = tracks;

		return &state->track;
	}

	if (state->focus == MH_MENU_RANKINGS_LAPS) {
		*count = MH_SINGLE_LAP_PRESET_COUNT;
		return &state->lap_choice;
	}

	if (state->focus == MH_MENU_RANKINGS_DIFFICULTY) {
		*count = MH_MENU_RANKINGS_DIFFICULTIES;
		return &state->difficulty;
	}

	*count = 2;

	return &state->record_class;
}

enum mh_error_code
mh_menu_rankings_update(struct mh_menu_rankings_state *state,
	unsigned int track_count, const struct mh_menu_rankings_input *input,
	struct mh_menu_rankings_output *output, struct mh_error *error_out)
{
	struct mh_menu_rankings_state next;
	struct mh_menu_rankings_output result;
	unsigned int *value, count, sound;

	if (!valid_state(state, track_count) || input == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Rankings input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (input->escape == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_MAIN);
	}

	if (input->up == 1) {
		value = selected_field(&next, track_count, &count);
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (*value != 0) {
			(*value)--;
			sound = MH_MENU_SOUND_ROW_PREVIOUS;
			result.reload = 1;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->down == 1) {
		value = selected_field(&next, track_count, &count);
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (*value < count - 1) {
			(*value)++;
			sound = MH_MENU_SOUND_ROW_NEXT;
			result.reload = 1;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->left == 1) {
		sound = next.focus == MH_MENU_RANKINGS_CATEGORY
			? MH_MENU_SOUND_COLUMN_LIMIT
			: MH_MENU_SOUND_COLUMN_PREVIOUS;
		append_event(&result, MH_MENU_SOUND, sound);

		if (next.focus == MH_MENU_RANKINGS_TRACK ||
			next.focus == MH_MENU_RANKINGS_LAPS)
			next.focus--;
		else if (next.focus == MH_MENU_RANKINGS_DIFFICULTY)
			next.focus = next.category == MH_HIGHSCORE_LAP ||
					next.category == MH_HIGHSCORE_LEAGUE
				? MH_MENU_RANKINGS_TRACK
				: MH_MENU_RANKINGS_LAPS;
		else if (next.focus == MH_MENU_RANKINGS_RECORD_CLASS)
			next.focus = next.category == MH_HIGHSCORE_LEAGUE
				? MH_MENU_RANKINGS_TRACK
				: next.category + 1;
	}

	if (input->right == 1 && next.focus != MH_MENU_RANKINGS_RECORD_CLASS) {
		sound = MH_MENU_SOUND_COLUMN_NEXT;

		if (next.focus == MH_MENU_RANKINGS_DIFFICULTY)
			sound = MH_MENU_SOUND_COLUMN_LIMIT;
		else if (next.focus == MH_MENU_RANKINGS_LAPS) {
			if (next.category < MH_HIGHSCORE_SINGLE)
				sound = MH_MENU_SOUND_COLUMN_LIMIT;
			else
				next.focus = MH_MENU_RANKINGS_DIFFICULTY;
		} else if (next.focus == MH_MENU_RANKINGS_TRACK) {
			if (next.category == MH_HIGHSCORE_LAP)
				sound = MH_MENU_SOUND_COLUMN_LIMIT;
			else if (next.category == MH_HIGHSCORE_LEAGUE)
				next.focus = MH_MENU_RANKINGS_DIFFICULTY;
			else
				next.focus = MH_MENU_RANKINGS_LAPS;
		} else
			next.focus = MH_MENU_RANKINGS_TRACK;

		append_event(&result, MH_MENU_SOUND, sound);
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_rankings_query(const struct mh_menu_rankings_state *state,
	const char *const *names, unsigned int track_count,
	struct mh_highscore_query *output, struct mh_error *error_out)
{
	struct mh_highscore_query next;
	char filename[MH_HIGHSCORE_FILENAME_SIZE];
	enum mh_error_code status;

	if (!valid_state(state, track_count) || output == NULL ||
		(state->category != MH_HIGHSCORE_LEAGUE && names == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Rankings table query");

	memset(&next, 0, sizeof(next));
	next.kind = state->category;
	next.laps = mh_single_lap_presets[state->lap_choice];
	next.difficulty = state->difficulty;
	next.record_class = state->record_class;
	next.division = state->division;

	if (state->category != MH_HIGHSCORE_LEAGUE)
		next.track = names[state->track];

	status = mh_highscore_filename(
		&next, filename, sizeof(filename), error_out);

	if (status == MH_ERROR_NONE)
		*output = next;

	return status;
}

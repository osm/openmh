#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/lap_presets.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_race_setup.h"

#define CATALOG_LIMIT 4096

enum mh_error_code
mh_menu_race_laps(unsigned long laps, int forward, unsigned long *output,
	struct mh_error *error_out)
{
	if (output == NULL || laps > MH_U32_MAX ||
		(forward != 0 && forward != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu lap selection");

	*output = mh_single_lap_step(laps, forward);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
append_event(
	struct mh_menu_output *result, unsigned int kind, unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

enum mh_error_code
mh_menu_race_setup_update(struct mh_menu_race_setup_state *state,
	unsigned int mode, unsigned int visible_tracks,
	unsigned int unlocked_tracks, const struct mh_menu_keys *input,
	struct mh_menu_output *output, struct mh_error *error_out)
{
	struct mh_menu_race_setup_state next;
	struct mh_menu_output result;
	unsigned long laps;
	unsigned int sound, last_row;

	if (state == NULL || input == NULL || output == NULL ||
		state->selected > MH_MENU_RACE_CHOICE_CATCH_UP ||
		state->catch_up > 1 || state->laps > MH_U32_MAX ||
		state->track_auxiliary > MH_U32_MAX || mode > MH_GAME_NETWORK ||
		visible_tracks == 0 || visible_tracks > CATALOG_LIMIT ||
		unlocked_tracks == 0 || unlocked_tracks > visible_tracks ||
		state->track >= visible_tracks)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race setup input");

	next = *state;
	next.track_auxiliary = 0;
	memset(&result, 0, sizeof(result));

	if (input->left == 1) {
		if (next.selected == MH_MENU_RACE_CHOICE_TRACK) {
			sound = MH_MENU_SOUND_VALUE_LIMIT;

			if (next.track != 0) {
				next.track--;
				sound = MH_MENU_SOUND_VALUE_PREVIOUS;
			}
		} else if (next.selected == MH_MENU_RACE_CHOICE_LAPS) {
			laps = mh_single_lap_step(next.laps, 0);
			sound = laps == next.laps
				? MH_MENU_SOUND_LAPS_PREVIOUS_LIMIT
				: MH_MENU_SOUND_LAPS_PREVIOUS;
			next.laps = laps;
		} else {
			sound = MH_MENU_SOUND_TOGGLE_LIMIT;

			if (next.catch_up == 1) {
				next.catch_up = 0;
				sound = MH_MENU_SOUND_TOGGLE_OFF;
			}
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->right == 1) {
		if (next.selected == MH_MENU_RACE_CHOICE_TRACK) {
			sound = MH_MENU_SOUND_VALUE_LIMIT;

			if (next.track < visible_tracks - 1) {
				next.track++;
				sound = MH_MENU_SOUND_VALUE_NEXT;
			}
		} else if (next.selected == MH_MENU_RACE_CHOICE_LAPS) {
			laps = mh_single_lap_step(next.laps, 1);
			sound = laps == next.laps
				? MH_MENU_SOUND_LAPS_NEXT_LIMIT
				: MH_MENU_SOUND_LAPS_NEXT;
			next.laps = laps;
		} else {
			sound = MH_MENU_SOUND_TOGGLE_LIMIT;

			if (next.catch_up == 0) {
				next.catch_up = 1;
				sound = MH_MENU_SOUND_TOGGLE_ON;
			}
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->up == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (next.selected != MH_MENU_RACE_CHOICE_TRACK) {
			next.selected--;
			sound = MH_MENU_SOUND_ROW_PREVIOUS;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->down == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;
		last_row = mode == MH_GAME_TIME_ATTACK
			? MH_MENU_RACE_CHOICE_LAPS
			: MH_MENU_RACE_CHOICE_CATCH_UP;

		if (next.selected < last_row) {
			next.selected++;
			sound = MH_MENU_SOUND_ROW_NEXT;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->enter == 1) {
		if (next.track < unlocked_tracks) {
			append_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
			append_event(&result, MH_MENU_PAGE, MH_MENU_CAR_SETUP);
		} else
			append_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_TRACK_LOCKED);
	}

	if (input->escape == 1) {
		if (next.track >= unlocked_tracks) {
			append_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_TRACK_LOCKED);
			next.track = unlocked_tracks - 1;
		}

		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_SINGLE);
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

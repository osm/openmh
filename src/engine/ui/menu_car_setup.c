#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_car_setup.h"

#define CATALOG_LIMIT 4096
#define HORN_LIMIT 128
#define STAGE_LIMIT 3
#define LEAGUE_DIVISIONS 4
#define DIVISION_BASE 100
#define SPECIAL_REMAINDER 10

static int
valid_catalog(const struct mh_menu_car_catalog *catalog)
{
	unsigned int i;

	if (catalog == NULL || catalog->divisions == NULL ||
		catalog->count == 0 || catalog->count > CATALOG_LIMIT ||
		catalog->visible == 0 || catalog->visible > catalog->count ||
		catalog->unlocked == 0 || catalog->unlocked > catalog->count ||
		catalog->saved_visible > catalog->count)
		return 0;

	for (i = 0; i < catalog->count; i++)
		if (catalog->divisions[i] > MH_U32_MAX)
			return 0;

	return 1;
}

enum mh_error_code
mh_menu_car_enter(struct mh_menu_car_setup_state *state,
	struct mh_menu_car_catalog *catalog, unsigned int mode,
	int special_allowed, struct mh_error *error_out)
{
	unsigned int ordinary, i, visible, saved, car;

	if (!valid_catalog(catalog) || state == NULL ||
		state->car >= catalog->count || mode > MH_GAME_NETWORK ||
		(special_allowed != 0 && special_allowed != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid car setup entry");

	visible = catalog->visible;
	saved = catalog->saved_visible;
	car = state->car;

	if (!special_allowed || mode == MH_GAME_LEAGUE) {
		ordinary = 0;

		for (i = 0; i < catalog->count; i++)
			if (catalog->divisions[i] % DIVISION_BASE <
				SPECIAL_REMAINDER)
				ordinary++;

		if (ordinary == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"car setup has no ordinary cars");

		saved = visible;
		visible = ordinary;

		if (car >= visible)
			car = visible - 1;

		/*
		 * With special cars disabled, League saves the already reduced
		 * count a second time, so leave retains it.
		 */
		if (!special_allowed && mode == MH_GAME_LEAGUE)
			saved = visible;
	}

	state->car = car;
	catalog->visible = visible;
	catalog->saved_visible = saved;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_car_leave(struct mh_menu_car_catalog *catalog, unsigned int mode,
	struct mh_error *error_out)
{
	if (!valid_catalog(catalog) || mode > MH_GAME_NETWORK ||
		(mode == MH_GAME_LEAGUE && catalog->saved_visible == 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car setup departure");

	if (mode == MH_GAME_LEAGUE)
		catalog->visible = catalog->saved_visible;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
append_event(struct mh_menu_car_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

enum mh_error_code
mh_menu_car_setup_update(struct mh_menu_car_setup_state *state,
	const struct mh_menu_car_catalog *catalog, unsigned int mode,
	unsigned int stage, unsigned int horn_count,
	const struct mh_menu_keys *input, struct mh_menu_car_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_car_setup_state next;
	struct mh_menu_car_output result;
	unsigned int sound, destination;

	if (!valid_catalog(catalog) || state == NULL || input == NULL ||
		output == NULL || state->selected > MH_MENU_CAR_CHOICE_RECORD ||
		state->car >= catalog->count || state->horn >= horn_count ||
		state->automatic > 1 || state->record > 1 ||
		mode > MH_GAME_NETWORK || stage > STAGE_LIMIT ||
		horn_count == 0 || horn_count > HORN_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid car setup input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (input->up == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (next.selected != MH_MENU_CAR_CHOICE_CAR) {
			next.selected--;
			sound = MH_MENU_SOUND_ROW_PREVIOUS;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->down == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (next.selected < MH_MENU_CAR_CHOICE_RECORD) {
			next.selected++;
			sound = MH_MENU_SOUND_ROW_NEXT;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->left == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (next.selected == MH_MENU_CAR_CHOICE_CAR) {
			if (next.car != 0) {
				next.car--;
				sound = MH_MENU_SOUND_VALUE_PREVIOUS;
			}
		} else if (next.selected == MH_MENU_CAR_CHOICE_HORN) {
			if (next.horn != 0) {
				next.horn--;
				sound = MH_MENU_SOUND_VALUE_PREVIOUS;
			}
		} else if (next.selected == MH_MENU_CAR_CHOICE_TRANSMISSION) {
			sound = next.automatic ? MH_MENU_SOUND_TOGGLE_OFF
					: MH_MENU_SOUND_TOGGLE_LIMIT;
			next.automatic = 0;
		} else {
			sound = next.record ? MH_MENU_SOUND_TOGGLE_OFF
					: MH_MENU_SOUND_TOGGLE_LIMIT;
			next.record = 0;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->right == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (next.selected == MH_MENU_CAR_CHOICE_CAR) {
			if (next.car < catalog->visible - 1) {
				next.car++;
				sound = MH_MENU_SOUND_VALUE_NEXT;
			}
		} else if (next.selected == MH_MENU_CAR_CHOICE_HORN) {
			if (next.horn < horn_count - 1) {
				next.horn++;
				sound = MH_MENU_SOUND_VALUE_NEXT;
			}
		} else if (next.selected == MH_MENU_CAR_CHOICE_TRANSMISSION) {
			sound = next.automatic ? MH_MENU_SOUND_TOGGLE_LIMIT
					: MH_MENU_SOUND_TOGGLE_ON;
			next.automatic = 1;
		} else {
			sound = next.record ? MH_MENU_SOUND_TOGGLE_LIMIT
					: MH_MENU_SOUND_TOGGLE_ON;
			next.record = 1;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->escape == 1) {
		if (next.car >= catalog->unlocked) {
			next.car = catalog->unlocked - 1;
			append_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_CAR_LOCKED);
		}

		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		destination = mode == MH_GAME_LEAGUE ? MH_MENU_LEAGUE_SETUP
			: mode == MH_GAME_GHOST	     ? MH_MENU_GHOST_SETUP
						: MH_MENU_RACE_SETUP;
		append_event(&result, MH_MENU_PAGE, destination);
	}

	if (input->enter == 1) {
		if (next.selected == MH_MENU_CAR_CHOICE_HORN)
			append_event(&result, MH_MENU_HORN, next.horn);
		else if (next.car >= catalog->unlocked)
			append_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_CAR_LOCKED);
		else if (mode != MH_GAME_LEAGUE ||
			catalog->divisions[next.car] / DIVISION_BASE <=
				LEAGUE_DIVISIONS - stage) {
			append_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
			append_event(&result, MH_MENU_PAGE, MH_MENU_QUICK);
		}

		/* League rejects an unlocked but over-division car silently. */
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

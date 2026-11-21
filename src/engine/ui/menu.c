#include <string.h>

#include "engine/core/limits.h"
#include "engine/game/league.h"
#include "engine/game/mode.h"
#include "engine/input/devices.h"
#include "engine/ui/menu.h"

#define POSITION_MIX_SCALE 256.0
#define TRANSITION_MS 150
#define MAIN_IDLE_MS 30000
#define PAGE_IDLE_MS 1000000000UL

static void
emit_sound(struct mh_menu *menu, unsigned int event)
{
	if (menu->hooks != NULL && menu->hooks->sound != NULL)
		menu->hooks->sound(menu->context, event);
}

static void
update_progress(struct mh_menu *menu)
{
	float fraction;

	fraction = (float)((double)menu->current->timer /
		(double)menu->duration_ms);

	if (fraction > 1.0f)
		fraction = 1.0f;

	menu->progress = fraction;
	menu->position_mix = (unsigned int)(POSITION_MIX_SCALE -
		(double)fraction * POSITION_MIX_SCALE);
}

void
mh_menu_init(struct mh_menu *menu, struct mh_menu_page *main,
	const struct mh_menu_hooks *hooks, void *context)
{
	memset(menu, 0, sizeof(*menu));

	menu->current = menu->main = main;
	menu->hooks = hooks;
	menu->context = context;
	menu->duration_ms = TRANSITION_MS;
	menu->idle_ms = MAIN_IDLE_MS;
}

void
mh_menu_restore_page(struct mh_menu *menu, struct mh_menu_page *standings,
	struct mh_menu_page *multi, int snapshot, unsigned int mode,
	unsigned int progression)
{
	menu->current = menu->main;

	if (snapshot) {
		if (mode == MH_GAME_LEAGUE && progression != MH_LEAGUE_CHAMPION)
			menu->current = standings;

		if (mode == MH_GAME_NETWORK)
			menu->current = multi;
	}

	menu->layout = menu->current->layout;
}

void
mh_menu_request(struct mh_menu *menu, struct mh_menu_page *page)
{
	if (menu->hooks != NULL && menu->hooks->input_mode != NULL)
		menu->hooks->input_mode(menu->context, MH_DEVICE_KEYS_MENU);

	menu->pending = page;
	menu->idle_ms = page == menu->main ? MAIN_IDLE_MS : PAGE_IDLE_MS;
	menu->leaving = 1;
	emit_sound(menu, MH_MENU_SOUND_PAGE_OUT);
}

int
mh_menu_step(struct mh_menu *menu)
{
	mh_menu_callback callback;

	if (menu->current == NULL)
		return 0;

	menu->layout = menu->current->layout;

	if (menu->current->timer == 0) {
		menu->leaving = 0;
		emit_sound(menu, MH_MENU_SOUND_PAGE_IN);
		callback = menu->current->init;

		if (callback != NULL)
			callback(menu, menu->context);

		menu->current->timer =
			(menu->current->timer + menu->elapsed_ms) & MH_U32_MAX;

		if (menu->current->timer == 0)
			menu->current->timer = 1;

		return 1;
	}

	if (menu->leaving) {
		if (menu->current->timer > menu->elapsed_ms) {
			update_progress(menu);
			callback = menu->current->draw;

			if (callback != NULL)
				callback(menu, menu->context);

			menu->current->timer =
				(menu->current->timer - menu->elapsed_ms) &
				MH_U32_MAX;
		} else {
			menu->current->timer = 0;
			update_progress(menu);
			callback = menu->current->draw;

			if (callback != NULL)
				callback(menu, menu->context);

			callback = menu->current->leave;

			if (callback != NULL)
				callback(menu, menu->context);

			menu->leaving = 0;
			menu->current = menu->pending;
		}
	} else {
		update_progress(menu);
		callback = menu->current->draw;

		if (callback != NULL)
			callback(menu, menu->context);

		if (menu->modal == 1) {
			if (menu->hooks != NULL && menu->hooks->modal != NULL)
				menu->hooks->modal(menu, menu->context);
		} else {
			callback = menu->current->input;

			if (callback != NULL)
				callback(menu, menu->context);
		}

		if (menu->current->timer < menu->duration_ms)
			menu->current->timer =
				(menu->current->timer + menu->elapsed_ms) &
				MH_U32_MAX;
	}

	return 1;
}

int
mh_menu_sound_flash(unsigned int event)
{
	return event == MH_MENU_SOUND_LIMIT ||
		event == MH_MENU_SOUND_ROW_LIMIT ||
		event == MH_MENU_SOUND_VALUE_LIMIT ||
		event == MH_MENU_SOUND_SLIDER_LIMIT ||
		event == MH_MENU_SOUND_TOGGLE_LIMIT ||
		event == MH_MENU_SOUND_LAPS_NEXT_LIMIT ||
		event == MH_MENU_SOUND_LAPS_PREVIOUS_LIMIT ||
		event == MH_MENU_SOUND_COLUMN_LIMIT ||
		event == MH_MENU_SOUND_BIND_REJECT ||
		event == MH_MENU_SOUND_TEXT_REJECT;
}

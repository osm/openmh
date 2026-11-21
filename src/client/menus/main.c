#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/ui/menu_main.h"

void
mh_menus_main_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_main_animation_init(&menus->layout,
			menus->main.selected, menu->position_mix,
			&menus->main_animation, &menus->error);
}

void
mh_menus_main_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_main_render(&menus->main_art, &menus->layout,
			menus->main.selected, menu->position_mix,
			menus->clock_ms, menu->elapsed_ms,
			&menus->main_animation, &menus->random_state,
			&menus->row_noise, &menus->work, &menus->main_choices,
			&menus->error);
}

void
mh_menus_main_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_input keys;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (menus->startup_dialog != 0) {
		mh_menus_open_dialog(menus, menus->startup_dialog);
		menus->startup_dialog = 0;
		return;
	}

	keys = menus->navigation_keys;

	if (!mh_menus_feature_enabled(
		&menus->configuration, MH_MENU_FEATURE_EXIT)) {
		keys.escape = 0;
		menus->main.exit_answer = 0;
	}

	if (mh_menu_main_update(&menus->main, &keys, &output,
		&menus->main_choices, &menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; i < output.count; i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_DIALOG)
			mh_menus_open_dialog(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_EXIT)
			mh_menus_request(menu, &menus->exit_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_SINGLE)
			mh_menus_request(menu, &menus->single_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_OPTIONS)
			mh_menus_request(menu, &menus->options_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_MULTI)
			mh_menus_request(menu, &menus->multi_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_CREDITS)
			mh_menus_request(menu, &menus->credits_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_RANKINGS)
			mh_menus_request(menu, &menus->rankings_page);
	}
}

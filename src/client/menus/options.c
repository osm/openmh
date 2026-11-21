#include "client/menus/internal.h"
#include "engine/core/error.h"

void
mh_menus_options_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;
	menus->options_entries++;
	menus->options.difficulty = menus->settings->difficulty;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_options_animation_init(&menus->layout,
			menus->options.selected, menu->position_mix,
			&menus->options_animation, &menus->error);
}

void
mh_menus_options_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_options_render(&menus->options_art,
			&menus->layout, &menus->options, menu->position_mix,
			menus->clock_ms, menu->elapsed_ms, 0,
			&menus->options_animation, &menus->random_state,
			&menus->row_noise, &menus->work,
			&menus->options_choices, &menus->error);
}

void
mh_menus_options_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_options_update(&menus->options, &menus->navigation_keys,
		&output, &menus->options_choices,
		&menus->error) != MH_ERROR_NONE)
		return;

	menus->settings->difficulty = menus->details.difficulty =
		menus->options.difficulty;

	for (i = 0; i < output.count && menus->error.code == MH_ERROR_NONE;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_MAIN)
			mh_menus_request(menu, &menus->main_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_SOUND_OPTIONS)
			mh_menus_request(menu, &menus->sound_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_GRAPHICS)
			mh_menus_request(menu, &menus->graphics_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_PERSONAL)
			mh_menus_request(menu, &menus->personal_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_CONTROLS)
			mh_menus_request(menu, &menus->controls_page);
	}
}

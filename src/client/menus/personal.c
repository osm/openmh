#include "client/menus/internal.h"
#include "engine/core/error.h"

void
mh_menus_personal_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->personal_entries++;
	menus->personal.patched = menus->personal_art.patched;
	mh_menu_personal_animation_init(&menus->personal_animation);
}

void
mh_menus_personal_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_personal_render(&menus->personal_art,
			&menus->font, &menus->layout, &menus->personal,
			menus->settings, menus->personal_colors,
			menu->position_mix, &menus->personal_animation,
			&menus->random_state, &menus->row_noise, menus->palette,
			&menus->work, &menus->error);
}

void
mh_menus_personal_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_personal_output output;
	unsigned int i;
	enum mh_error_code status;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	menus->personal_keys.clock_ms = menus->clock_ms;
	status = mh_menu_personal_update(&menus->personal, menus->settings,
		menus->personal_colors, &menus->personal_keys,
		menus->font.advance, &output, &menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_personal_overlay(&menus->font, &menus->layout,
			&output, menu->position_mix, menus->palette,
			&menus->work, &menus->error);

	if (status != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_OPTIONS)
			mh_menus_request(menu, &menus->options_page);
	}
}

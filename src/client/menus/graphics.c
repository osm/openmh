#include "client/menus/internal.h"
#include "engine/core/error.h"

void
mh_menus_graphics_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	const struct mh_menu_graphics_catalog *catalog;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->graphics_entries++;
	menus->graphics.fps_editing = 0;

	catalog = mh_graphics_catalog_choices(menus->graphics_catalog);

	if (!catalog->devices[menus->graphics.device].glenz &&
		menus->settings->av.car_shading == MH_AV_SHADING_GLENZ)
		menus->settings->av.car_shading = MH_AV_SHADING_REFLECTION;

	mh_menu_graphics_animation_init(&menus->graphics_animation);
}

void
mh_menus_graphics_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_graphics_render(&menus->graphics_art,
			&menus->font, &menus->layout, &menus->graphics,
			&menus->settings->av,
			mh_graphics_catalog_choices(menus->graphics_catalog),
			menu->position_mix, &menus->graphics_animation,
			&menus->random_state, &menus->row_noise, &menus->work,
			&menus->error);
}

void
mh_menus_graphics_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	menus->graphics_keys.elapsed_ms = menu->elapsed_ms;

	if (mh_menu_graphics_update(&menus->graphics, &menus->settings->av,
		mh_graphics_catalog_choices(menus->graphics_catalog),
		&menus->graphics_keys, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; i < output.count && menus->error.code == MH_ERROR_NONE;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_OPTIONS)
			mh_menus_request(menu, &menus->options_page);
	}
}

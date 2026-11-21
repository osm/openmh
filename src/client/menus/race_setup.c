#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/ui/menu_palette.h"

void
mh_menus_single_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;
	menus->single_entries++;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_single_animation_init(&menus->layout,
			menus->single.selected, menu->position_mix,
			&menus->single_animation, &menus->error);
}

void
mh_menus_single_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_single_render(&menus->single_art, &menus->font,
			&menus->layout, &menus->single, &menus->details,
			menu->position_mix, menus->clock_ms, menu->elapsed_ms,
			&menus->single_animation, &menus->random_state,
			&menus->row_noise, &menus->work, &menus->single_choices,
			&menus->error);

	if (menus->error.code == MH_ERROR_NONE) {
		/*
		 * Publish the shared animation frames. Ghost draws with
		 * those frames before preparing its own next-frame
		 * selections.
		 */
		memcpy(menus->ghost_animation.frames,
			menus->single_animation.frames,
			sizeof(menus->ghost_animation.frames));
		mh_menus_trace_frames(
			menus, "single", menus->single_animation.frames);
	}
}

void
mh_menus_single_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_single_update(&menus->single, menus->recordings.menu.count,
		&menus->navigation_keys, &output, &menus->single_choices,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_MAIN)
			mh_menus_request(menu, &menus->main_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_RACE_SETUP)
			mh_menus_request(menu, &menus->race_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_GHOST_SETUP)
			mh_menus_request(menu, &menus->ghost_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_LEAGUE_SETUP)
			mh_menus_league_request(menus, MH_MENU_LEAGUE_SETUP);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_QUICK)
			mh_menus_request_launch(menus);
	}
}

void
mh_menus_single_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->single_leaves++;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_single_leave(&menus->single, &menus->error);
}

void
mh_menus_race_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->race_entries++;
	menus->race.track = menus->details.track;
	menus->race.laps = menus->details.laps;
	menus->race_scene.mode = menus->single.mode;
	mh_menu_race_animation_init(&menus->race_animation);
}

void
mh_menus_race_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	/*
	 * Sound's course-list movement sets its preview flag; its next draw
	 * clears it, including the departure draw. Track setup therefore
	 * inherits zero.
	 */
	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_race_render(&menus->race_art, &menus->font,
			&menus->layout, &menus->race, &menus->race_scene,
			menu->position_mix, menus->clock_ms, menu->elapsed_ms,
			0, &menus->race_animation, &menus->digits,
			&menus->random_state, &menus->row_noise, menus->palette,
			&menus->work, &menus->error);
}

void
mh_menus_race_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_race_setup_update(&menus->race, menus->single.mode,
		menus->race_scene.count, menus->race_scene.unlocked,
		&menus->setup_keys, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_SINGLE)
			mh_menus_request(menu, &menus->single_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_CAR_SETUP)
			mh_menus_request(menu, &menus->car_page);
	}
}

void
mh_menus_race_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->race_leaves++;
	menus->details.track = menus->race.track;
	menus->details.laps = (unsigned int)menus->race.laps;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_palette_base(menus->race_art.palette,
			menus->backdrop.palette, menus->palette, &menus->error);
}

void
mh_menus_car_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->car_entries++;
	menus->car.car = menus->single.car;
	menus->car.automatic = menus->details.automatic;
	menus->car_scene.mode = menus->single.mode;
	menus->car_scene.stage = menus->details.stage;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_car_enter(&menus->car, &menus->car_scene.catalog,
			menus->single.mode, menus->special_cars, &menus->error);

	mh_menu_car_animation_init(&menus->car_animation);
}

void
mh_menus_car_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_car_render(&menus->car_art, &menus->font,
			&menus->layout, &menus->car, &menus->car_scene,
			menu->position_mix, menus->clock_ms, menu->elapsed_ms,
			0, &menus->car_animation, &menus->random_state,
			&menus->row_noise, menus->palette, &menus->work,
			&menus->error);

	if (menus->error.code == MH_ERROR_NONE) {
		/* Car -> Ghost is the other original entry edge. */
		memcpy(menus->ghost_animation.frames,
			menus->car_animation.frames,
			sizeof(menus->ghost_animation.frames));
		mh_menus_trace_frames(
			menus, "car", menus->car_animation.frames);
	}
}

void
mh_menus_car_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_car_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_car_setup_update(&menus->car, &menus->car_scene.catalog,
		menus->single.mode, menus->details.stage,
		menus->horns.count, &menus->setup_keys, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_HORN) {
			menus->horn_previews++;

			if (!mh_menu_audio_horn(
				menus->audio, output.events[i].value))
				mh_error_set(&menus->error, MH_ERROR_BACKEND,
					"cannot play horn preview");
		} else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_RACE_SETUP)
			mh_menus_request(menu, &menus->race_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_GHOST_SETUP)
			mh_menus_request(menu, &menus->ghost_page);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_LEAGUE_SETUP)
			mh_menus_league_request(menus, MH_MENU_LEAGUE_SETUP);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_QUICK)
			mh_menus_request_launch(menus);
	}
}

void
mh_menus_car_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->car_leaves++;
	menus->single.car = menus->car.car;
	menus->details.automatic = menus->car.automatic;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_car_leave(&menus->car_scene.catalog,
			menus->single.mode, &menus->error);

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_palette_base(menus->car_art.palette,
			menus->backdrop.palette, menus->palette, &menus->error);
}

#include "client/menus/internal.h"
#include "engine/core/error.h"

/* Artwork layout IDs, distinct from entry pages and destinations. */
enum {
	LAYOUT_RACE = 2,
	LAYOUT_CAR = 3,
	LAYOUT_GRAPHICS = 5,
	LAYOUT_SOUND = 6,
	LAYOUT_OPTIONS = 7,
	LAYOUT_CONTROLS = 8,
	LAYOUT_MULTI = 9,
	LAYOUT_SINGLE = 10,
	LAYOUT_RANKINGS = 11,
	LAYOUT_LEAGUE_SETUP = 12,
	LAYOUT_LEAGUE_NEW = 13,
	LAYOUT_LEAGUE_LOAD = 14,
	LAYOUT_LEAGUE_DELETE = 15,
	LAYOUT_LEAGUE_STANDINGS = 16,
	LAYOUT_PERSONAL = 17,
	LAYOUT_CREDITS = 18,
	LAYOUT_GHOST = 19,
	LAYOUT_RECORDING = 24,
	LAYOUT_ATTRACT = 28,
	LAYOUT_ACTION = 31
};

static void
bind_callbacks(struct mh_menus *menus)
{
	static const unsigned int layouts[MH_MENUS_LEAGUE_COUNT] = {
		LAYOUT_LEAGUE_SETUP, LAYOUT_LEAGUE_STANDINGS,
		LAYOUT_LEAGUE_LOAD, LAYOUT_LEAGUE_NEW, LAYOUT_LEAGUE_DELETE};

	unsigned int i;

	menus->main_page.init = mh_menus_main_init;
	menus->main_page.draw = mh_menus_main_draw;
	menus->main_page.input = mh_menus_main_input;

	menus->options_page.layout = LAYOUT_OPTIONS;
	menus->options_page.init = mh_menus_options_init;
	menus->options_page.draw = mh_menus_options_draw;
	menus->options_page.input = mh_menus_options_input;

	menus->sound_page.layout = LAYOUT_SOUND;
	menus->sound_page.init = mh_menus_sound_init;
	menus->sound_page.draw = mh_menus_sound_draw;
	menus->sound_page.input = mh_menus_sound_input;
	menus->sound_page.leave = mh_menus_sound_leave;

	menus->graphics_page.layout = LAYOUT_GRAPHICS;
	menus->graphics_page.init = mh_menus_graphics_init;
	menus->graphics_page.draw = mh_menus_graphics_draw;
	menus->graphics_page.input = mh_menus_graphics_input;

	menus->credits_page.layout = LAYOUT_CREDITS;
	menus->credits_page.init = mh_menus_credits_init;
	menus->credits_page.draw = mh_menus_credits_draw;
	menus->credits_page.input = mh_menus_credits_input;

	menus->rankings_page.layout = LAYOUT_RANKINGS;
	menus->rankings_page.init = mh_menus_rankings_init;
	menus->rankings_page.draw = mh_menus_rankings_draw;
	menus->rankings_page.input = mh_menus_rankings_input;
	menus->rankings_page.leave = mh_menus_rankings_leave;

	menus->personal_page.layout = LAYOUT_PERSONAL;
	menus->personal_page.init = mh_menus_personal_init;
	menus->personal_page.draw = mh_menus_personal_draw;
	menus->personal_page.input = mh_menus_personal_input;

	menus->controls_page.layout = LAYOUT_CONTROLS;
	menus->controls_page.init = mh_menus_controls_init;
	menus->controls_page.draw = mh_menus_controls_draw;
	menus->controls_page.input = mh_menus_controls_input;

	menus->multi_page.layout = LAYOUT_MULTI;
	menus->multi_page.init = mh_menus_multiplayer_init;
	menus->multi_page.draw = mh_menus_multiplayer_draw;
	menus->multi_page.input = mh_menus_multiplayer_input;
	menus->multi_page.leave = mh_menus_multiplayer_leave;

	menus->single_page.layout = LAYOUT_SINGLE;
	menus->single_page.init = mh_menus_single_init;
	menus->single_page.draw = mh_menus_single_draw;
	menus->single_page.input = mh_menus_single_input;
	menus->single_page.leave = mh_menus_single_leave;

	menus->race_page.layout = LAYOUT_RACE;
	menus->race_page.init = mh_menus_race_init;
	menus->race_page.draw = mh_menus_race_draw;
	menus->race_page.input = mh_menus_race_input;
	menus->race_page.leave = mh_menus_race_leave;

	menus->car_page.layout = LAYOUT_CAR;
	menus->car_page.init = mh_menus_car_init;
	menus->car_page.draw = mh_menus_car_draw;
	menus->car_page.input = mh_menus_car_input;
	menus->car_page.leave = mh_menus_car_leave;

	menus->ghost_page.layout = LAYOUT_GHOST;
	menus->ghost_page.init = mh_menus_ghost_init;
	menus->ghost_page.draw = mh_menus_ghost_draw;
	menus->ghost_page.input = mh_menus_ghost_input;

	menus->exit_page.layout = LAYOUT_ACTION;
	menus->exit_page.input = mh_menus_exit_input;

	menus->launch_page.layout = LAYOUT_ACTION;
	menus->launch_page.input = mh_menus_launch_input;

	menus->attract_page.layout = LAYOUT_ATTRACT;
	menus->attract_page.input = mh_menus_launch_input;

	menus->save_page.layout = LAYOUT_RECORDING;
	menus->save_page.input = mh_menus_save_input;

	for (i = 0; i < MH_MENUS_LEAGUE_COUNT; i++) {
		menus->league_page[i].layout = layouts[i];
		menus->league_page[i].init = mh_menus_league_init;
		menus->league_page[i].draw = mh_menus_league_draw;
		menus->league_page[i].input = mh_menus_league_input;
	}
}

static void
select_start_page(struct mh_menus *menus, enum mh_menus_start_page start_page)
{
	struct mh_menu_page *page;

	switch (start_page) {
	case MH_MENUS_PAGE_SINGLE:
		page = &menus->single_page;
		break;

	case MH_MENUS_PAGE_SETUP:
		page = &menus->race_page;
		break;

	case MH_MENUS_PAGE_CAR:
		page = &menus->car_page;
		break;

	case MH_MENUS_PAGE_GHOST:
		page = &menus->ghost_page;
		break;

	case MH_MENUS_PAGE_OPTIONS:
		page = &menus->options_page;
		break;

	case MH_MENUS_PAGE_SOUND:
		page = &menus->sound_page;
		break;

	case MH_MENUS_PAGE_GRAPHICS:
		page = &menus->graphics_page;
		break;

	case MH_MENUS_PAGE_PERSONAL:
		page = &menus->personal_page;
		break;

	case MH_MENUS_PAGE_CONTROLS:
		page = &menus->controls_page;
		break;

	case MH_MENUS_PAGE_MULTI:
		page = &menus->multi_page;
		break;

	case MH_MENUS_PAGE_CREDITS:
		page = &menus->credits_page;
		break;

	case MH_MENUS_PAGE_RANKINGS:
		page = &menus->rankings_page;
		break;

	case MH_MENUS_PAGE_LEAGUE:
	case MH_MENUS_PAGE_STANDINGS:
	case MH_MENUS_PAGE_LEAGUE_LOAD:
	case MH_MENUS_PAGE_LEAGUE_NEW:
	case MH_MENUS_PAGE_LEAGUE_DELETE:
		page = &menus->league_page[start_page - MH_MENUS_PAGE_LEAGUE];
		break;

	default:
		return;
	}

	if (!mh_menus_page_enabled(menus, page))
		return;

	menus->menu.current = page;
	menus->menu.layout = page->layout;

	if (start_page == MH_MENUS_PAGE_GHOST)
		menus->single.mode = MH_GAME_GHOST;

	if (start_page >= MH_MENUS_PAGE_LEAGUE &&
		start_page <= MH_MENUS_PAGE_LEAGUE_DELETE)
		menus->single.mode = MH_GAME_LEAGUE;
}

void
mh_menus_bind_pages(struct mh_menus *menus, enum mh_menus_start_page start_page)
{
	static const struct mh_menu_hooks hooks = {mh_menus_sound_event,
		mh_menus_set_input_mode, mh_menus_dialog_step};

	mh_menus_configuration_bind(menus);

	if (mh_menus_compact(menus)) {
		mh_menus_compact_bind(menus);
		return;
	}

	bind_callbacks(menus);
	mh_menu_init(&menus->menu, &menus->main_page, &hooks, menus);
	mh_menu_restore_page(&menus->menu,
		&menus->league_page[MH_MENUS_LEAGUE_STANDINGS],
		&menus->multi_page, 0, 0, 0);
	select_start_page(menus, start_page);
}

void
mh_menus_sound_event(void *context, unsigned int event)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE && mh_menu_sound_flash(event))
		(void)mh_menu_background_flash(&menus->background,
			&menus->random_state, &menus->error);

	if (menus->error.code == MH_ERROR_NONE &&
		!mh_menu_audio_play(menus->audio, event))
		mh_error_set(&menus->error, MH_ERROR_BACKEND,
			"cannot play frontend sound");
}

void
mh_menus_set_input_mode(void *context, unsigned int mode)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;
	menus->input_mode = mode;
}

void
mh_menus_exit_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->exit_requested = 1;
}

void
mh_menus_launch_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->launch_requested = mh_menus_page_enabled(menus, menu->current);
}

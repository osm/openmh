#include "client/menus/internal.h"

/*
 * CFG tokenization cannot preserve arbitrary filesystem names. Retain
 * the prior saved choice for those names; browsing still accepts them.
 */
static int
cfg_name_supported(const char *name)
{
	size_t i, length;
	int representable;

	length = strlen(name);
	representable = 1;

	for (i = 0; i < length; i++)
		if ((unsigned char)name[i] < 32 || name[i] == ';' ||
			(name[i] == ' ' &&
				(i == 0 || i + 1 == length ||
					name[i - 1] == ' ')))
			representable = 0;

	return representable;
}

void
mh_menus_publish_settings(
	const struct mh_menus *menus, struct mh_settings *settings)
{
	const struct mh_league_catalog *catalog;
	const struct mh_menu_graphics_catalog *graphics;
	const struct mh_menu_graphics_mode *mode;
	const char *league_name;

	/* Publish the selected mode before CFG save. */
	graphics = mh_graphics_catalog_choices(menus->graphics_catalog);
	mode = &graphics->devices[menus->graphics.device]
			.modes[menus->graphics.mode];
	settings->renderer = menus->graphics.device;
	settings->av.width = mode->width;
	settings->av.height = mode->height;

	/*
	 * Also retain current edits on a native window close, without invoking
	 * a page departure that could change the selected game mode.
	 */
	settings->car =
		menus->car_numbers[menus->menu.current == &menus->car_page
				? menus->car.car
				: menus->single.car] -
		1;
	settings->track =
		menus->track_numbers[menus->menu.current == &menus->race_page
				? menus->race.track
				: menus->details.track] -
		1;
	settings->laps = menus->menu.current == &menus->race_page
		? (unsigned int)menus->race.laps
		: menus->details.laps;
	settings->automatic = menus->menu.current == &menus->car_page
		? menus->car.automatic
		: menus->details.automatic;
	settings->game_type = menus->menu.current == &menus->ghost_page
		? menus->ghost.mode & 3
		: menus->single.mode;
	settings->catch_up = menus->race.catch_up;
	settings->horn = menus->car.horn;
	settings->record_race = menus->car.record;
	settings->menu_confirmation = menus->navigation_keys.confirmation;

	if (cfg_name_supported(menus->demo_name))
		strcpy(settings->demo_filename, menus->demo_name);

	catalog = mh_leagues_catalog(menus->leagues);

	if (catalog != NULL &&
		mh_menus_league_page_index(menus) != MH_MENUS_LEAGUE_NEW) {
		league_name = catalog->entries[catalog->selected].document.name;

		if (cfg_name_supported(league_name))
			strcpy(settings->league_name, league_name);
	}
}

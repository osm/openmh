#include "client/menus/internal.h"
#include "engine/core/error.h"

static void
reload_rankings(struct mh_menus *menus)
{
	struct mh_highscore_query query;
	struct mh_error error, diagnostic;
	enum mh_error_code status;
	char filename[MH_HIGHSCORE_FILENAME_SIZE];
	int present;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	menus->rankings_loaded = 0;
	present = 0;
	status = mh_menu_rankings_query(&menus->rankings, menus->track_titles,
		menus->track_catalog.count, &query, &error);

	if (status == MH_ERROR_NONE)
		status = mh_highscore_filename(
			&query, filename, sizeof(filename), &error);

	if (status == MH_ERROR_NONE && menus->rankings_directory != NULL)
		status = mh_highscore_file_load_from(menus->user_data,
			menus->rankings_directory, &query,
			&menus->rankings_table, &present, &error);
	else if (status == MH_ERROR_NONE)
		status = mh_highscore_defaults(
			query.kind, query.laps, &menus->rankings_table, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Rankings: %s\n", error.message);
		mh_error_set(&diagnostic, MH_ERROR_IO,
			"Could not read this score table. Select another ranking or press Escape to return.");
		mh_menus_show_error(menus, diagnostic);
		return;
	}

	menus->rankings_loaded = 1;
	menus->rankings_loads++;
	printf("Rankings: entry %u, load %u, file %s, present %d\n",
		menus->rankings_entries, menus->rankings_loads, filename,
		present);
	fflush(stdout);
}

void
mh_menus_rankings_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->rankings_entries++;
	mh_menu_rankings_animation_init(&menus->rankings_animation);

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_rankings_init(&menus->rankings,
			menus->single.mode, menus->details.track,
			menus->details.laps, menus->details.difficulty,
			(unsigned int)menus->track_unlocks.record_class,
			menus->track_catalog.count, &menus->error);

	reload_rankings(menus);
}

void
mh_menus_rankings_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_rankings_render(&menus->rankings_art,
			&menus->font, &menus->layout, &menus->rankings,
			menus->track_titles, menus->track_catalog.count,
			menus->rankings_loaded ? &menus->rankings_table : NULL,
			menu->position_mix, &menus->rankings_animation,
			&menus->random_state, &menus->row_noise, &menus->work,
			&menus->error);
}

void
mh_menus_rankings_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_rankings_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_rankings_update(&menus->rankings,
		menus->track_catalog.count, &menus->rankings_keys, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0;
		menus->error.code == MH_ERROR_NONE && i < output.events.count;
		i++) {
		if (output.events.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(
				menus, output.events.events[i].value);
		else if (output.events.events[i].kind == MH_MENU_PAGE)
			mh_menus_request(menu, &menus->main_page);
	}

	if (menus->error.code == MH_ERROR_NONE && output.reload)
		reload_rankings(menus);
}

void
mh_menus_rankings_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->rankings_loaded = 0;
	memset(&menus->rankings_table, 0, sizeof(menus->rankings_table));
}

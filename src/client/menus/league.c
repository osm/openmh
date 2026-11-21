#include <stdlib.h>

#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/game/league_names.h"
#include "interfaces/clock/clock.h"
#include "interfaces/storage/user_data.h"

const unsigned int mh_menus_league_destinations[MH_MENUS_LEAGUE_COUNT] = {
	MH_MENU_LEAGUE_SETUP, MH_MENU_LEAGUE_STANDINGS, MH_MENU_LEAGUE_LOAD,
	MH_MENU_LEAGUE_NEW, MH_MENU_LEAGUE_DELETE};

enum mh_error_code
mh_menus_league_creation(struct mh_menus *menus,
	struct mh_league_creation *creation, struct mh_error *error_out)
{
	struct mh_clock_date date;
	enum mh_error_code status;

	if (menus->profiles == NULL || menus->settings == NULL ||
		menus->single.car >= menus->car_catalog.count ||
		menus->timer == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"League profiles/preferences are unavailable");

	creation->profiles = menus->profiles->profiles;
	creation->count = menus->profiles->count;
	creation->player = menus->settings->player_name;
	creation->car = menus->cars[menus->single.car].name;
	creation->color = menus->settings->player_color;

	status = mh_clock_source_date(menus->timer->source, &date, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_league_date(date.year - 1900, date.month - 1, date.day,
		&creation->date, error_out);
}

enum mh_error_code
mh_menus_league_sync(struct mh_menus *menus, struct mh_error *error_out)
{
	const struct mh_league_catalog *catalog;
	enum mh_error_code status;
	unsigned int i;

	catalog = mh_leagues_catalog(menus->leagues);

	if (catalog == NULL || catalog->count == 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"League catalog is unavailable");

	menus->league.count = catalog->count;
	menus->league.selected = catalog->selected;

	for (i = 0; i < catalog->count; i++) {
		status = mh_menu_league_info_read(&catalog->entries[i].document,
			&menus->league_info[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	menus->details.stage = menus->league_info[catalog->selected].stage;
	menus->details.race_index =
		(unsigned int)menus->league_info[catalog->selected].race_index;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_default_directory(struct mh_menus *menus, struct mh_error *error_out)
{
	char *root, path[sizeof(menus->league_path)];
	size_t length, separator;
	enum mh_error_code status;

	status = mh_user_data_directory(menus->user_data, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(root);
	separator = length != 0 && root[length - 1] != '/';

	if (length > sizeof(path) - 1 - separator) {
		free(root);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"League directory is too long");
	}

	memcpy(path, root, length);
	free(root);

	if (separator)
		path[length++] = '/';

	path[length] = 0;
	status = mh_user_data_create_directory(
		menus->user_data, path, error_out);

	if (status == MH_ERROR_NONE)
		strcpy(menus->league_path, path);

	return status;
}

enum mh_error_code
mh_menus_league_prepare(struct mh_menus *menus, struct mh_error *error_out)
{
	struct mh_league_creation creation;
	enum mh_error_code status;
	const char *directory;

	if (menus->leagues != NULL)
		return mh_menus_league_sync(menus, error_out);

	if (menus->profiles == NULL) {
		status = mh_menus_profiles_load(menus, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	directory = menus->league_directory;

	if (directory == NULL && !menus->capture_only) {
		status = prepare_default_directory(menus, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		directory = menus->league_path;
	}

	status = mh_menus_league_creation(menus, &creation, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_open_from(menus->user_data,
			menus->asset_root, directory, &creation,
			menus->settings->league_name, &menus->random_state,
			&menus->leagues, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menus_league_sync(menus, error_out);
}

unsigned int
mh_menus_league_page_index(const struct mh_menus *menus)
{
	unsigned int page_index;

	for (page_index = 0; page_index < MH_MENUS_LEAGUE_COUNT; page_index++)
		if (menus->menu.current == &menus->league_page[page_index])
			break;

	return page_index;
}

enum mh_error_code
mh_menus_league_reload(struct mh_menus *menus, struct mh_error *error_out)
{
	struct mh_league_creation creation;
	const char *directory;
	enum mh_error_code status;

	if (menus->leagues == NULL)
		return mh_menus_league_prepare(menus, error_out);

	directory = menus->league_directory;

	if (directory == NULL && menus->league_path[0] != 0)
		directory = menus->league_path;

	status = mh_menus_league_creation(menus, &creation, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_reload_from(menus->user_data,
			menus->asset_root, directory, &creation,
			menus->settings->league_name, &menus->random_state,
			&menus->leagues, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menus_league_sync(menus, error_out);
}

void
mh_menus_league_request(struct mh_menus *menus, unsigned int destination)
{
	struct mh_error error;
	unsigned int page_index;

	for (page_index = 0; page_index < MH_MENUS_LEAGUE_COUNT; page_index++)
		if (mh_menus_league_destinations[page_index] == destination)
			break;

	if (page_index == MH_MENUS_LEAGUE_COUNT) {
		mh_error_set(
			&menus->error, MH_ERROR_INVALID, "unknown League page");
		return;
	}

	if (mh_menus_league_prepare(menus, &error) != MH_ERROR_NONE) {
		mh_menus_show_error(menus, error);
		return;
	}

	mh_menus_request(&menus->menu, &menus->league_page[page_index]);
}

void
mh_menus_league_init(struct mh_menu *menu, void *context)
{
	struct mh_league_creation creation;
	const struct mh_league_catalog *catalog;
	struct mh_menus *menus;
	unsigned int page_index;

	menus = (struct mh_menus *)context;
	page_index = mh_menus_league_page_index(menus);

	if (page_index == MH_MENUS_LEAGUE_COUNT) {
		mh_error_set(
			&menus->error, MH_ERROR_INVALID, "unknown League page");
		return;
	}

	menus->league_entries[page_index]++;
	menus->single.mode = MH_GAME_LEAGUE;

	if (page_index == MH_MENUS_LEAGUE_NEW) {
		if (mh_menus_league_creation(menus, &creation, &menus->error) !=
			MH_ERROR_NONE)
			return;

		if (mh_leagues_create(menus->leagues, &creation,
			&menus->random_state,
			&menus->error) != MH_ERROR_NONE)
			return;
	}

	if (mh_menus_league_sync(menus, &menus->error) != MH_ERROR_NONE)
		return;

	if (page_index == MH_MENUS_LEAGUE_NEW) {
		catalog = mh_leagues_catalog(menus->leagues);
		menus->league.new_row = 0;
		menus->league.create_answer = 0;
		memset(menus->league.name, 0, sizeof(menus->league.name));
		strcpy((char *)menus->league.name,
			catalog->entries[catalog->selected].document.name);
	}

	(void)mh_menu_league_animation_init(
		mh_menus_league_destinations[page_index], &menus->layout,
		menus->league.choice, menu->position_mix,
		&menus->league_animation[page_index], &menus->error);

	if (menus->error.code == MH_ERROR_NONE) {
		printf("League page: %u, count %u, selected %u\n", page_index,
			menus->league.count, menus->league.selected);
		fflush(stdout);
	}
}

void
mh_menus_league_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_league_details details;
	unsigned int page_index;

	menus = (struct mh_menus *)context;
	page_index = mh_menus_league_page_index(menus);

	if (menus->error.code != MH_ERROR_NONE)
		return;

	(void)mh_menus_league_sync(menus, &menus->error);
	details.leagues = menus->league_info;
	details.cars = menus->cars;
	details.tracks = menus->tracks;
	details.car_count = menus->car_catalog.count;
	details.track_count = menus->track_catalog.count;
	details.profile_count = menus->profiles->count;

	if (menus->error.code == MH_ERROR_NONE &&
		page_index < MH_MENUS_LEAGUE_COUNT)
		(void)mh_menu_league_render(&menus->league_art[page_index],
			&menus->font, &menus->layout, &menus->league, &details,
			&menus->single.car, menus->palette, menu->position_mix,
			menus->clock_ms, menu->elapsed_ms,
			&menus->league_animation[page_index],
			&menus->random_state, &menus->row_noise, &menus->work,
			&menus->error);
}

static enum mh_error_code
apply_league_removal(struct mh_menus *menus, unsigned int index,
	struct mh_menu_league_output *next)
{
	struct mh_league_creation creation;
	struct mh_error error;
	enum mh_error_code status;

	status = mh_menus_league_creation(menus, &creation, &error);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_remove(menus->leagues, index, &creation,
			&menus->random_state, &error);

	if (status != MH_ERROR_NONE) {
		menus->league.pending_delete = MH_MENU_LEAGUE_REMOVE_IDLE;
		menus->league.create_answer = menus->league.delete_answer = 0;
		mh_menus_show_error(menus, error);
		return status;
	}

	printf("League removed: count %u, selected %u\n",
		mh_leagues_catalog(menus->leagues)->count,
		mh_leagues_catalog(menus->leagues)->selected);
	fflush(stdout);
	status = mh_menus_league_sync(menus, &menus->error);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menu_league_finish_remove(&menus->league, menus->league.count,
		menus->league.selected, next, &menus->error);
}

static enum mh_error_code
save_new_league(
	struct mh_menus *menus, int discarded_draft, struct mh_error *error_out)
{
	const struct mh_league_catalog *catalog;
	const struct mh_league_document *document;
	const unsigned char *name;
	enum mh_error_code status;

	catalog = mh_leagues_catalog(menus->leagues);
	name = menus->league.name;

	if (discarded_draft) {
		document = &catalog->entries[catalog->selected].document;
		name = (const unsigned char *)document->name;
	}

	status = mh_leagues_set_name(menus->leagues, name, error_out);

	if (status == MH_ERROR_NONE && !menus->capture_only)
		status = mh_leagues_save(menus->leagues, error_out);

	return status;
}

static void
apply_league_events(struct mh_menus *menus,
	const struct mh_menu_league_output *output, int discarded_draft)
{
	struct mh_menu_league_output next;
	struct mh_error error;
	unsigned int i;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output->count;
		i++) {
		if (output->events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_DIALOG)
			mh_menus_open_dialog(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_LEAGUE_SELECT)
			(void)mh_leagues_select(menus->leagues,
				output->events[i].value, &menus->error);
		else if (output->events[i].kind == MH_MENU_LEAGUE_REMOVE) {
			discarded_draft = menus->league.pending_delete ==
				MH_MENU_LEAGUE_REMOVE_DRAFT;

			if (apply_league_removal(menus, output->events[i].value,
				&next) != MH_ERROR_NONE)
				return;

			apply_league_events(menus, &next, discarded_draft);
		} else if (output->events[i].kind == MH_MENU_PAGE) {
			if (mh_menus_league_page_index(menus) ==
				MH_MENUS_LEAGUE_NEW) {
				if (save_new_league(menus, discarded_draft,
					&error) != MH_ERROR_NONE) {
					mh_menus_show_error(menus, error);
					return;
				}
			}

			if (output->events[i].value == MH_MENU_SINGLE)
				mh_menus_request(
					&menus->menu, &menus->single_page);
			else if (output->events[i].value == MH_MENU_CAR_SETUP)
				mh_menus_request(
					&menus->menu, &menus->car_page);
			else
				mh_menus_league_request(
					menus, output->events[i].value);
		}
	}
}

void
mh_menus_league_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_league_output output;
	unsigned int page_index;
	enum mh_error_code status;

	menus = (struct mh_menus *)context;
	page_index = mh_menus_league_page_index(menus);

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (page_index == MH_MENUS_LEAGUE_SETUP)
		status = mh_menu_league_update(&menus->league,
			&menus->navigation_keys, &output, &menus->error);
	else if (page_index == MH_MENUS_LEAGUE_STANDINGS)
		status = mh_menu_league_standings_update(&menus->league,
			&menus->navigation_keys, &output, &menus->error);
	else if (page_index == MH_MENUS_LEAGUE_LOAD)
		status = mh_menu_league_load_update(&menus->league,
			&menus->setup_keys, &output, &menus->error);
	else if (page_index == MH_MENUS_LEAGUE_NEW) {
		menus->league_input.clock_ms = menus->clock_ms;
		menus->league_input.confirmation =
			menus->navigation_keys.confirmation;
		status = mh_menu_league_new_update(&menus->league,
			&menus->league_input, menus->font.advance, &output,
			&menus->error);

		if (status == MH_ERROR_NONE)
			status = mh_menu_league_cursor(&menus->layout,
				menu->position_mix, &output, &menus->font,
				&menus->work, &menus->error);
	} else
		status = mh_menu_league_delete_update(&menus->league,
			&menus->setup_keys, menus->navigation_keys.confirmation,
			&output, &menus->error);

	if (status == MH_ERROR_NONE)
		apply_league_events(menus, &output, 0);
}

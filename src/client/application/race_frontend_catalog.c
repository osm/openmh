#include <stdio.h>
#include <string.h>

#include "client/application/race_frontend_internal.h"
#include "client/session/progress.h"
#include "client/settings/controls.h"
#include "client/settings/render_choice.h"
#include "engine/assets/path_menus.h"
#include "engine/audio/race_profile.h"
#include "engine/game/cheats.h"
#include "engine/settings/course_music.h"
#include "engine/ui/menu_catalog.h"
#include "engine/ui/menu_unlocks.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/image/palette.h"
#include "interfaces/window/window.h"

static enum mh_error_code
load_track_availability(struct mh_application *app, int unlock,
	struct mh_menu_track_unlocks *tracks, unsigned long available[],
	struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT],
		sorted[MH_TRACK_CATALOG_LIMIT];
	unsigned int order[MH_TRACK_CATALOG_LIMIT], i;
	char progress[MH_PROGRESS_PATH_SIZE];
	enum mh_error_code status;

	f = app->race_frontend;
	status = mh_progress_path_from(
		app->user_data, app->progress_directory, progress, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < f->tracks.count; i++)
		divisions[i] = f->tracks.entries[i].division >=
				MH_TRACK_CUSTOM_DIVISION
			? 0
			: f->tracks.entries[i].division;

	status = mh_menu_catalog_order(divisions, f->tracks.count, order,
		MH_TRACK_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < f->tracks.count; i++) {
		sorted[i] = f->tracks.entries[order[i]].division;
		status = mh_progress_available_from(app->resources,
			app->user_data, app->asset_root, progress,
			&f->tracks.entries[order[i]], &available[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (unlock)
			available[i] = 1;
	}

	status = mh_menu_track_unlocks(
		sorted, available, f->tracks.count, 0, 1, tracks, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < tracks->unlocked; i++)
		f->track_available[order[i]] = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_car_availability(struct mh_application *app, int special,
	const struct mh_menu_track_unlocks *tracks,
	const unsigned long available[], struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	struct mh_menu_car_unlocks cars;
	unsigned long car_divisions[MH_CAR_CATALOG_LIMIT];
	unsigned int car_order[MH_CAR_CATALOG_LIMIT], i;
	enum mh_error_code status;

	f = app->race_frontend;

	for (i = 0; i < f->cars.count; i++)
		car_divisions[i] = f->cars.entries[i].division;

	status = mh_menu_catalog_order(car_divisions, f->cars.count, car_order,
		MH_CAR_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < f->cars.count; i++)
		car_divisions[i] = f->cars.entries[car_order[i]].division;

	status = mh_menu_car_unlocks(car_divisions, f->cars.count, 0,
		tracks->divisions, available, f->tracks.count, tracks->visible,
		tracks->unlocked, &cars, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < cars.unlocked; i++)
		f->car_available[car_order[i]] =
			special || car_divisions[i] % 100 < 10;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_availability(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	struct mh_menu_track_unlocks tracks;
	unsigned long available[MH_TRACK_CATALOG_LIMIT];
	int unlock, special;
	enum mh_error_code status;

	f = app->race_frontend;

	if (app->content_edition->menu == MH_CONTENT_MENU_COMPACT) {
		memset(f->car_available, 1, f->cars.count);
		memset(f->track_available, 1, f->tracks.count);
		f->record_class = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	unlock = app->unlock_all ||
		mh_cheat_unlock_tracks(app->preferences->player_name,
			app->preferences->team_name);
	special = app->unlock_all ||
		mh_progress_special_cars_available(
			app->resources, app->asset_root);
	status = load_track_availability(
		app, unlock, &tracks, available, error_out);

	if (status == MH_ERROR_NONE)
		status = load_car_availability(
			app, special, &tracks, available, error_out);

	if (status == MH_ERROR_NONE)
		f->record_class = (unsigned int)tracks.record_class;

	return status;
}

static enum mh_error_code
load_graphics(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_window_system_options options;
	enum mh_error_code status;
	unsigned int selected;

	if (!app->window_acquired) {
		options.offscreen = 0;
		options.preserve_environment = 1;
		status = mh_window_system_acquire(
			app->windows, &options, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		app->window_acquired = 1;
	}

	return mh_graphics_catalog_open_from(app->windows,
		app->preferences->av.width, app->preferences->av.height,
		mh_render_choice_modes(), MH_RENDER_CHOICE_COUNT, 0, 0, 0,
		&app->race_frontend->graphics, &selected, error_out);
}

static enum mh_error_code
load_name_colors(struct mh_application *app, struct mh_error *error_out)
{
	unsigned long palette[MH_PALETTE_COLORS];
	unsigned int i;
	enum mh_error_code status;

	status = mh_palette_load_from(app->session_options.palette_source,
		app->resources, app->asset_root, MH_ASSET_MENU_PALETTE, palette,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* The personal menu presents the last 32 palette entries in reverse. */
	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++)
		app->race_frontend->name_colors[i] =
			palette[MH_PALETTE_COLORS - 1 - i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_application_name_color(const struct mh_application *app, unsigned int index)
{
	if (app == NULL || app->race_frontend == NULL ||
		index >= MH_MENU_PERSONAL_COLORS)
		return -1;

	return (int)app->race_frontend->name_colors[index];
}

enum mh_error_code
mh_race_frontend_catalogs(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	struct mh_error music_error;
	unsigned int i;
	enum mh_error_code status;

	f = app->race_frontend;
	status = mh_car_catalog_load_from(app->session_options.catalog_source,
		app->resources, app->asset_root, &f->cars, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_load_from(
			app->session_options.catalog_source, app->resources,
			app->asset_root, &f->tracks, error_out);

	if (status == MH_ERROR_NONE)
		status = load_availability(app, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_horn_catalog_load_from(
			app->session_options.horn_source, app->resources,
			app->asset_root, &f->horns, error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_control_profiles_open(app->resources, app->user_data,
				app->asset_root, app->controls_directory,
				app->no_settings ? MH_CONTROL_SESSION
						: MH_CONTROL_PERSISTENT,
				app->preferences->control_name, &f->controls,
				&f->control_store, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_settings_music_bind(app->preferences,
			f->tracks.entries, f->tracks.count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < f->tracks.count; i++)
		f->music[i] = mh_settings_track_music(
			app->preferences, &f->tracks.entries[i]);

	/* Packages without music remain playable. */
	(void)mh_music_catalog_scan_from(app->audio_decoder, app->resources,
		app->asset_root, app->session_options.music_root,
		&f->music_catalog, &music_error);

	status = load_name_colors(app, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return load_graphics(app, error_out);
}

unsigned int
mh_application_race_count(
	const struct mh_application *app, enum mh_frontend_catalog kind)
{
	if (app == NULL || app->race_frontend == NULL)
		return 0;

	switch (kind) {
	case MH_FRONTEND_CARS:
		return app->race_frontend->cars.count;
	case MH_FRONTEND_TRACKS:
		return app->race_frontend->tracks.count;
	case MH_FRONTEND_AUDIO_PROFILES:
		return MH_RACE_AUDIO_A3D20 + 1;
	case MH_FRONTEND_RENDERERS:
		return MH_RENDER_CHOICE_COUNT;
	case MH_FRONTEND_CONTROLS:
		return app->race_frontend->controls.count;
	case MH_FRONTEND_MODES:
		return 3;
	case MH_FRONTEND_HORNS:
		return app->race_frontend->horns.count;
	case MH_FRONTEND_MUSIC:
		return app->race_frontend->music_catalog.last_track > 1
			? app->race_frontend->music_catalog.last_track - 1
			: 0;
	}

	return 0;
}

const char *
mh_application_race_choice(const struct mh_application *app,
	enum mh_frontend_catalog kind, unsigned int index, int *available)
{
	const struct mh_race_frontend *f;
	static char music_label[64];
	unsigned long seconds;

	if (available == NULL || index >= mh_application_race_count(app, kind))
		return NULL;

	f = app->race_frontend;
	*available = 1;

	switch (kind) {
	case MH_FRONTEND_CARS:
		*available = f->car_available[index];
		return f->cars.entries[index].name;

	case MH_FRONTEND_TRACKS:
		*available = f->track_available[index];
		return f->tracks.entries[index].name;

	case MH_FRONTEND_AUDIO_PROFILES:
		*available = mh_race_audio_available(index);
		return mh_race_audio_name(index);

	case MH_FRONTEND_RENDERERS:
		*available = mh_render_choice_available(index);
		return mh_render_choice_names()[index];

	case MH_FRONTEND_CONTROLS:
		return f->controls.profiles[index].layout.name;
	case MH_FRONTEND_HORNS:
		return f->horns.horns[index].name;
	case MH_FRONTEND_MUSIC:
		seconds = f->music_catalog.seconds[index + 2];
		*available = seconds != 0;
		sprintf(music_label, "Track %u (%lu:%02lu)", index + 2,
			seconds / 60, seconds % 60);
		return music_label;

	case MH_FRONTEND_MODES:
		*available = index == 0 ||
			(index == 2 &&
				(app->content_edition->features &
					MH_CONTENT_RACE_MODES));
		return index == 0    ? "Quick Race"
			: index == 2 ? "Time Attack"
				: "League";
	}

	return NULL;
}

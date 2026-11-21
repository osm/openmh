#include <stdio.h>

#include "client/menus/internal.h"
#include "interfaces/storage/user_data.h"

/* Raw capture: RGB palette followed by the original 640 x 400 indexed frame. */
enum mh_error_code
mh_menus_save_indexed(const char *path, const struct mh_menus *menus,
	const struct mh_user_data *data, struct mh_error *error_out)
{
	unsigned char *bytes;
	size_t size;
	enum mh_error_code status;

	size = sizeof(menus->present_palette) + MH_MENUS_PIXELS;
	bytes = malloc(size);

	if (bytes == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory capturing indexed frame");

	memcpy(bytes, menus->present_palette, sizeof(menus->present_palette));
	memcpy(bytes + sizeof(menus->present_palette), menus->present.pixels,
		MH_MENUS_PIXELS);
	status = mh_user_data_replace(data, path, bytes, size, error_out);
	free(bytes);

	return status;
}

static const char *
options_page_name(const struct mh_menus *menus, const struct mh_menu_page *page,
	const char *name)
{
	if (menus->menu.current == page)
		return name;

	if (menus->menu.current == &menus->options_page)
		return "options";

	return "main";
}

static void
print_options_summary(const struct mh_menus *menus)
{
	if (menus->controls_entries)
		printf("Controls options: entries %u, profile %u/%u, column %u, row %u, phase %u, mode %u, name %s, page %s\n",
			menus->controls_entries, menus->controls.selected,
			menus->controls_catalog.count, menus->controls.column,
			menus->controls.row, menus->controls.phase,
			menus->input_mode,
			menus->controls_catalog
				.profiles[menus->controls.selected]
				.layout.name,
			options_page_name(
				menus, &menus->controls_page, "controls"));

	if (menus->personal_entries)
		printf("Personal options: entries %u, focus %u, custom %u, player %06lx, "
		"body %06lx/%06lx/%06lx, page %s\n",
			menus->personal_entries, menus->personal.selected,
			menus->settings->custom_car_colors,
			menus->settings->player_color,
			menus->settings->car_colors[0],
			menus->settings->car_colors[1],
			menus->settings->car_colors[2],
			options_page_name(
				menus, &menus->personal_page, "personal"));

	if (menus->graphics_entries) {
		const struct mh_menu_graphics_catalog *catalog;
		const struct mh_menu_graphics_mode *mode;

		catalog = mh_graphics_catalog_choices(menus->graphics_catalog);
		mode = &catalog->devices[menus->graphics.device]
				.modes[menus->graphics.mode];
		printf("Graphics options: entries %u, focus %u, device %u, mode %u/%u, size %lux%lu, detail %u, page %s\n",
			menus->graphics_entries, menus->graphics.selected,
			menus->graphics.device, menus->graphics.mode,
			catalog->devices[menus->graphics.device].count,
			mode->width, mode->height,
			menus->settings->av.detail_mode,
			options_page_name(
				menus, &menus->graphics_page, "graphics"));
	}

	if (menus->options_entries || menus->sound_entries)
		printf("Sound options: entries %u/%u, effects %u, music %u, loop %u, "
		"difficulty %u, course %u CD %lu, current CD %lu, last CD %u, page %s\n",
			menus->options_entries, menus->sound_entries,
			menus->settings->av.effects_volume,
			menus->settings->av.music_volume,
			menus->settings->av.cd_loop,
			menus->settings->difficulty,
			menus->track_numbers[menus->sound.course],
			menus->course_music[menus->sound.course],
			mh_menu_audio_current_track(menus->audio),
			menus->music_catalog.last_track,
			options_page_name(menus, &menus->sound_page, "sound"));
}

static void
print_ghost_summary(const struct mh_menus *menus)
{
	const char *page;

	if (!menus->ghost_entries)
		return;

	if (menus->menu.current == &menus->ghost_page)
		page = "ghost";
	else if (menus->menu.current == &menus->car_page)
		page = "car";
	else if (menus->menu.current == &menus->single_page)
		page = "single";
	else
		page = "main";

	printf("Ghost setup: focus %u, mode %u, entries %u, page %s\n",
		menus->ghost.focus, menus->ghost.mode, menus->ghost_entries,
		page);
}

static void
print_single_summary(const struct mh_menus *menus)
{
	const char *page;

	if (!menus->single_entries)
		return;

	if (menus->menu.current == &menus->single_page)
		page = "single";
	else if (menus->menu.current == &menus->main_page)
		page = "main";
	else if (menus->menu.current == &menus->race_page)
		page = "setup";
	else if (menus->menu.current == &menus->car_page)
		page = "car";
	else if (menus->menu.current == &menus->ghost_page)
		page = "ghost";
	else
		page = "exit";

	printf("Single player: selection %u, mode %u, car %u, entries %u, leaves %u, page %s\n",
		menus->single.selected, menus->single.mode,
		menus->car_numbers[menus->single.car], menus->single_entries,
		menus->single_leaves, page);
}

static void
print_track_summary(const struct mh_menus *menus)
{
	const char *page;

	if (!menus->race_entries)
		return;

	if (menus->menu.current == &menus->race_page)
		page = "setup";
	else if (menus->menu.current == &menus->single_page)
		page = "single";
	else if (menus->menu.current == &menus->car_page)
		page = "car";
	else
		page = "main";

	printf("Track setup: selection %u, track %u, laps %lu, catch-up %u, unlocked %u, entries %u, leaves %u, page %s\n",
		menus->race.selected, menus->track_numbers[menus->race.track],
		menus->race.laps, menus->race.catch_up,
		menus->race_scene.unlocked, menus->race_entries,
		menus->race_leaves, page);
}

static void
print_car_summary(const struct mh_menus *menus)
{
	const char *page;

	if (!menus->car_entries)
		return;

	if (menus->menu.current == &menus->car_page)
		page = "car";
	else if (menus->menu.current == &menus->race_page)
		page = "setup";
	else if (menus->menu.current == &menus->single_page)
		page = "single";
	else if (menus->menu.current == &menus->ghost_page)
		page = "ghost";
	else
		page = "main";

	printf("Car setup: selection %u, car %u, index %u, automatic %u, record %u, horn %s, previews %u, "
	"visible %u, unlocked %u, entries %u, leaves %u, page %s\n",
		menus->car.selected, menus->car_numbers[menus->car.car],
		menus->car.car, menus->car.automatic, menus->car.record,
		menus->horns.horns[menus->car.horn].name, menus->horn_previews,
		menus->car_scene.catalog.visible,
		menus->car_scene.catalog.unlocked, menus->car_entries,
		menus->car_leaves, page);
}

void
mh_menus_print_summary(
	const struct mh_menus *menus, const struct mh_settings *preferences)
{
	if (menus->credits_entries)
		printf("Credits: entries %u, page %s\n", menus->credits_entries,
			menus->menu.current == &menus->credits_page ? "credits"
				: menus->menu.current == &menus->main_page
				? "main"
				: "other");

	if (menus->rankings_entries)
		printf("Rankings: entries %u, loads %u, focus %u, page %s\n",
			menus->rankings_entries, menus->rankings_loads,
			menus->rankings.focus,
			menus->menu.current == &menus->rankings_page
				? "rankings"
				: menus->menu.current == &menus->main_page
				? "main"
				: "other");

	print_options_summary(menus);

	if (menus->unlock_cheat)
		puts("Unlock cheat active; no markers written");

	if (preferences != NULL)
		printf("Settings: name %s, team %s, difficulty %u, effects %u, confirmation %u\n",
			preferences->player_name, preferences->team_name,
			menus->details.difficulty,
			preferences->av.effects_volume,
			menus->navigation_keys.confirmation);

	printf("Catalog: availability %02x, tracks %u/%u, cars %u/%u, record class %lu\n",
		menus->availability, menus->track_unlocks.unlocked,
		menus->track_unlocks.visible, menus->car_unlocks.unlocked,
		menus->car_unlocks.visible, menus->track_unlocks.record_class);
	printf("Menu preview: selection %u, seed %lu, row noise %lu\n",
		menus->main.selected, menus->random_state, menus->row_noise);

	if (menus->single_entries || menus->ghost_entries)
		printf("Demo catalog: count %u, selected %u, name %s\n",
			menus->recordings.menu.count,
			menus->recordings.menu.selected, menus->demo_name);

	print_ghost_summary(menus);

	if (menus->dialog_kind != 0)
		printf("Dialog: kind %u, visible %u, selected %u, answer %lu\n",
			menus->dialog_kind, menus->modal.visible,
			menus->modal.selected, menus->modal.answer);

	if (menus->exit_requested)
		puts("Exit page completed");

	print_single_summary(menus);
	print_track_summary(menus);
	print_car_summary(menus);
}

void
mh_menus_trace_frames(const struct mh_menus *menus, const char *page,
	const unsigned int frames[MH_MENU_LAYOUT_SLOTS])
{
	unsigned int i;

	if (!menus->debug.menu_frames)
		return;

	fprintf(stderr, "Menu frames: %s mix %u clock %lu:", page,
		menus->menu.position_mix, menus->clock_ms);

	for (i = 0; i < MH_MENU_LAYOUT_SLOTS; i++)
		fprintf(stderr, " %u", frames[i]);

	fputc('\n', stderr);
}

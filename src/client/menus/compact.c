#include <stdio.h>
#include <string.h>

#include "client/menus/internal.h"
#include "client/settings/render_choice.h"
#include "engine/ui/menu_text.h"

enum compact_row {
	ROW_START,
	ROW_CAR,
	ROW_TRACK,
	ROW_LAPS,
	ROW_DIFFICULTY,
	ROW_TRANSMISSION,
	ROW_CONTROLS,
	ROW_VOLUME,
	ROW_RENDERER,
	ROW_RESOLUTION,
	ROW_EXIT,
	ROW_COUNT
};

enum {
	COLOR_BACKGROUND,
	COLOR_TEXT,
	COLOR_SELECTED,
	COLOR_DISABLED,
	ROW_TOP = 74,
	ROW_HEIGHT = 22,
	LABEL_X = 52,
	VALUE_X = 280,
	TEXT_RIGHT = 610,
	VOLUME_STEP = 17
};

int
mh_menus_compact(const struct mh_menus *menus)
{
	return menus->edition != NULL &&
		menus->edition->menu == MH_CONTENT_MENU_COMPACT;
}

/* Prepared compact catalogs contain only the edition's playable choices. */
void
mh_menus_compact_choices(struct mh_menus *menus)
{
	menus->track_unlocks.selected = menus->details.track;
	menus->track_unlocks.visible = menus->track_unlocks.unlocked =
		menus->track_catalog.count;
	menus->track_unlocks.record_class = 1;
	menus->car_unlocks.selected = menus->single.car;
	menus->car_unlocks.visible = menus->car_unlocks.unlocked =
		menus->car_catalog.count;
	menus->race_scene.count = menus->race_scene.unlocked =
		menus->track_catalog.count;
	menus->car_scene.catalog.visible = menus->car_scene.catalog.unlocked =
		menus->car_catalog.count;
}

/* Fit native labels to the visible canvas; the text API clips bytes. */
static void
draw_text(struct mh_menus *menus, const char *text, unsigned int x,
	unsigned int y, unsigned int color)
{
	char fitted[256];
	size_t count;
	unsigned int width, advance;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	width = x;

	for (count = 0; count < sizeof(fitted) - 1 && text[count] != 0;
		count++) {
		advance = menus->font.advance[(unsigned char)text[count]];

		if (advance > TEXT_RIGHT - width)
			break;

		fitted[count] = text[count];
		width += advance;
	}

	fitted[count] = 0;
	(void)mh_menu_text_draw(&menus->font, (const unsigned char *)fitted,
		count + 1, x, y, color, &menus->present, &menus->error);
}

static const char *
row_value(const struct mh_menus *menus, enum compact_row row, char number[16])
{
	static const char *const difficulty[] = {"Easy", "Medium", "Hard"};
	const struct mh_menu_graphics_catalog *choices;
	const struct mh_menu_graphics_mode *size;

	switch (row) {
	case ROW_CAR:
		return menus->car_titles[menus->single.car];
	case ROW_TRACK:
		return menus->track_titles[menus->details.track];
	case ROW_LAPS:
		sprintf(number, "%u", menus->details.laps);
		return number;

	case ROW_DIFFICULTY:
		return difficulty[menus->settings->difficulty];
	case ROW_TRANSMISSION:
		return menus->details.automatic ? "Automatic" : "Manual";
	case ROW_CONTROLS:
		return menus->controls_catalog
			.profiles[menus->controls.selected]
			.layout.name;
	case ROW_VOLUME:
		sprintf(number, "%u%%",
			menus->settings->av.effects_volume * 100 /
				MH_AV_VOLUME_MAX);
		return number;

	case ROW_RENDERER:
		return mh_render_choice_names()[menus->settings->renderer];

	case ROW_RESOLUTION:
		choices = mh_graphics_catalog_choices(menus->graphics_catalog);
		size = &choices->devices[menus->graphics.device]
				.modes[menus->graphics.mode];
		sprintf(number, "%lux%lu", size->width, size->height);
		return number;

	default:
		return "";
	}
}

static void
draw_page(struct mh_menu *menu, void *context)
{
	static const char *const labels[ROW_COUNT] = {"Start race", "Car",
		"Course", "Laps", "Difficulty", "Transmission", "Controls",
		"Sound volume", "Renderer", "Resolution", "Quit"};

	static const unsigned char palette[][3] = {
		{14, 19, 28}, {194, 205, 216}, {255, 187, 66}, {94, 101, 110}};

	struct mh_menus *menus;
	unsigned int i, color, y;
	char number[16];

	(void)menu;
	menus = context;
	memset(menus->present.pixels, COLOR_BACKGROUND, menus->present.size);
	memcpy(menus->present_palette, palette, sizeof(palette));
	draw_text(menus, "MOTORHEAD", LABEL_X, 24, COLOR_SELECTED);
	draw_text(menus, "Prepared demo", VALUE_X, 24, COLOR_TEXT);

	for (i = 0; i < ROW_COUNT; i++) {
		if (mh_menu_choice_state(&menus->compact_choices, i) ==
			MH_MENU_HIDDEN)
			continue;

		y = ROW_TOP + i * ROW_HEIGHT;
		color = menus->compact_row == i ? COLOR_SELECTED : COLOR_TEXT;

		if (mh_menu_choice_state(&menus->compact_choices, i) ==
			MH_MENU_DISABLED)
			color = COLOR_DISABLED;

		draw_text(menus, labels[i], LABEL_X, y, color);
		draw_text(menus, row_value(menus, (enum compact_row)i, number),
			VALUE_X, y, color);
	}

	draw_text(menus, "Up/Down: select   Left/Right: change", LABEL_X, 330,
		COLOR_TEXT);
	draw_text(menus,
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_EXIT)
			? "Enter: choose   Escape: quit"
			: "Enter: choose",
		LABEL_X, 352, COLOR_TEXT);
	draw_text(menus, menus->compact_notice, LABEL_X, 378, COLOR_SELECTED);
}

static unsigned int
cycle_choice(unsigned int value, unsigned int count, int forward)
{
	if (forward)
		return value + 1 < count ? value + 1 : 0;

	return value == 0 ? count - 1 : value - 1;
}

static void
change_volume(struct mh_menus *menus, int forward)
{
	unsigned int volume;

	volume = menus->settings->av.effects_volume;

	if (forward)
		volume = volume > MH_AV_VOLUME_MAX - VOLUME_STEP
			? MH_AV_VOLUME_MAX
			: volume + VOLUME_STEP;
	else
		volume = volume < VOLUME_STEP ? 0 : volume - VOLUME_STEP;

	menus->settings->av.effects_volume = volume;

	if (!mh_menu_audio_effects_volume(menus->audio, volume))
		mh_error_set(&menus->error, MH_ERROR_BACKEND,
			"cannot update menu sound volume");
}

static void
change_renderer(struct mh_menus *menus, int forward)
{
	const struct mh_menu_graphics_catalog *choices;
	const struct mh_menu_graphics_mode *size;
	struct mh_graphics_catalog *catalog;
	unsigned int choice, mode;

	choice = cycle_choice(
		menus->settings->renderer, MH_RENDER_CHOICE_COUNT, forward);

	choices = mh_graphics_catalog_choices(menus->graphics_catalog);
	size = &choices->devices[menus->graphics.device]
			.modes[menus->graphics.mode];

	/*
	 * Re-select a valid size; software supports fewer modes than GPU
	 * profiles.
	 */
	if (mh_graphics_catalog_open_from(menus->windows, size->width,
		size->height, mh_render_choice_modes(),
		MH_RENDER_CHOICE_COUNT, choice,
		mh_menus_feature_enabled(&menus->configuration,
			MH_MENU_FEATURE_PRESENTATION),
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_SCREENSHOTS),
		&catalog, &mode, &menus->error) != MH_ERROR_NONE)
		return;

	mh_graphics_catalog_close(menus->graphics_catalog);
	menus->graphics_catalog = catalog;
	menus->graphics.device = menus->settings->renderer = choice;
	menus->graphics.mode = mode;
	choices = mh_graphics_catalog_choices(catalog);

	if (!choices->devices[choice].glenz &&
		menus->settings->av.car_shading == MH_AV_SHADING_GLENZ)
		menus->settings->av.car_shading = MH_AV_SHADING_REFLECTION;
}

static void
change_resolution(struct mh_menus *menus, int forward)
{
	const struct mh_menu_graphics_catalog *choices;
	unsigned int count;

	choices = mh_graphics_catalog_choices(menus->graphics_catalog);
	count = choices->devices[menus->graphics.device].count;
	menus->graphics.mode =
		cycle_choice(menus->graphics.mode, count, forward);
}

static void
change_choice(struct mh_menus *menus, int forward)
{
	unsigned long laps;

	switch (menus->compact_row) {
	case ROW_CAR:
		menus->single.car = cycle_choice(
			menus->single.car, menus->car_catalog.count, forward);
		break;

	case ROW_TRACK:
		menus->details.track = cycle_choice(menus->details.track,
			menus->track_catalog.count, forward);
		break;

	case ROW_LAPS:
		if (mh_menu_race_laps(menus->details.laps, forward, &laps,
			&menus->error) == MH_ERROR_NONE)
			menus->details.laps = (unsigned int)laps;

		break;

	case ROW_DIFFICULTY:
		menus->settings->difficulty =
			cycle_choice(menus->settings->difficulty, 3, forward);
		menus->details.difficulty = menus->settings->difficulty;
		break;

	case ROW_TRANSMISSION:
		menus->details.automatic = !menus->details.automatic;
		break;

	case ROW_CONTROLS:
		menus->controls.selected =
			cycle_choice(menus->controls.selected,
				menus->controls_catalog.count, forward);
		break;

	case ROW_VOLUME:
		change_volume(menus, forward);
		break;

	case ROW_RENDERER:
		change_renderer(menus, forward);
		break;

	case ROW_RESOLUTION:
		change_resolution(menus, forward);
		break;

	default:
		break;
	}
}

static void
read_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	const struct mh_menu_keys *keys;

	menus = context;
	keys = &menus->setup_keys;

	if (keys->escape) {
		mh_menus_request(menu, &menus->exit_page);
		return;
	}

	if (keys->up || keys->down)
		menus->compact_row = mh_menu_choice_move(
			&menus->compact_choices, menus->compact_row, ROW_COUNT,
			keys->down != 0, 1);

	if (mh_menu_choice_state(&menus->compact_choices, menus->compact_row) !=
		MH_MENU_ENABLED)
		return;

	if (keys->left || keys->right)
		change_choice(menus, keys->right != 0);

	if (!keys->enter)
		return;

	if (menus->compact_row == ROW_START) {
		menus->compact_notice[0] = 0;
		mh_menus_request_launch(menus);
	} else if (menus->compact_row == ROW_EXIT)
		mh_menus_request(menu, &menus->exit_page);
	else
		change_choice(menus, 1);
}

void
mh_menus_compact_bind(struct mh_menus *menus)
{
	/* No retail decoration, dialogs or attract playback is required. */
	memset(&menus->main_page, 0, sizeof(menus->main_page));
	memset(&menus->launch_page, 0, sizeof(menus->launch_page));
	memset(&menus->exit_page, 0, sizeof(menus->exit_page));
	menus->main_page.draw = draw_page;
	menus->main_page.input = read_input;
	menus->launch_page.input = mh_menus_launch_input;
	menus->exit_page.input = mh_menus_exit_input;
	mh_menu_init(&menus->menu, &menus->main_page, NULL, menus);
}

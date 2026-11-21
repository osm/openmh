#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/menus/internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Menu configuration line %d: %s\n",    \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
resolution(void)
{
	struct mh_menu_configuration config;
	struct mh_content_edition edition;
	unsigned int i;
	static const char *const demos[] = {"motordemo2_3dfx_intro",
		"motordemo2_soft", "motordemo_3dfx", "motordemo_3dfx_intro",
		"motordemo_mplayer", "motordemo_soft", "motordemo_soft_intro"};

	memset(&config, 0, sizeof(config));
	mh_menus_configuration_resolve(
		&config, mh_content_edition_find("retail-europe"), 1, 1, 1);

	for (i = 0; i < MH_MENU_FEATURE_COUNT; i++)
		CHECK(config.items[i] == MH_MENU_ENABLED);

	mh_menus_configuration_resolve(
		&config, mh_content_edition_find("retail-europe"), 0, 0, 0);
	CHECK(config.items[MH_MENU_FEATURE_NETWORK] == MH_MENU_HIDDEN);
	CHECK(config.items[MH_MENU_FEATURE_SOUND] == MH_MENU_DISABLED);
	CHECK(config.items[MH_MENU_FEATURE_FEEDBACK] == MH_MENU_DISABLED);
	CHECK(config.items[MH_MENU_FEATURE_LEAGUE] == MH_MENU_ENABLED);

	for (i = 0; i < sizeof(demos) / sizeof(demos[0]); i++) {
		memset(&config, 0, sizeof(config));
		edition = *mh_content_edition_find(demos[i]);

		/* Changing decoration must not change content eligibility. */
		edition.menu = MH_CONTENT_MENU_RETAIL;
		mh_menus_configuration_resolve(&config, &edition, 1, 1, 1);
		CHECK(config.items[MH_MENU_FEATURE_NETWORK] == MH_MENU_HIDDEN);
		CHECK(config.items[MH_MENU_FEATURE_LEAGUE] == MH_MENU_HIDDEN);
		CHECK(config.items[MH_MENU_FEATURE_GHOST] == MH_MENU_HIDDEN);
		CHECK(config.items[MH_MENU_FEATURE_ATTRACT] == MH_MENU_HIDDEN);
		CHECK(config.items[MH_MENU_FEATURE_QUICK] == MH_MENU_ENABLED);
		CHECK(config.items[MH_MENU_FEATURE_CONTROLS] ==
			MH_MENU_ENABLED);
	}

	memset(&config, 0, sizeof(config));
	config.items[MH_MENU_FEATURE_SINGLE] = MH_MENU_HIDDEN;
	config.items[MH_MENU_FEATURE_OPTIONS] = MH_MENU_DISABLED;
	config.items[MH_MENU_FEATURE_FEEDBACK] = MH_MENU_HIDDEN;
	config.items[MH_MENU_FEATURE_PRESENTATION] = MH_MENU_HIDDEN;
	mh_menus_configuration_resolve(&config, NULL, 1, 1, 1);
	CHECK(config.items[MH_MENU_FEATURE_QUICK] == MH_MENU_HIDDEN);
	CHECK(config.items[MH_MENU_FEATURE_GHOST] == MH_MENU_HIDDEN);
	CHECK(config.items[MH_MENU_FEATURE_CONTROLS] == MH_MENU_DISABLED);
	CHECK(config.items[MH_MENU_FEATURE_FEEDBACK] == MH_MENU_HIDDEN);
	CHECK(config.items[MH_MENU_FEATURE_PRESENTATION] == MH_MENU_HIDDEN);

	return 0;
}

static int
navigation(struct mh_menus *menus)
{
	struct mh_menu_input input;
	struct mh_menu_output output;
	unsigned int i;

	memset(&input, 0, sizeof(input));
	menus->configuration.items[MH_MENU_FEATURE_NETWORK] = MH_MENU_HIDDEN;
	menus->configuration.items[MH_MENU_FEATURE_RANKINGS] = MH_MENU_DISABLED;
	mh_menus_configuration_bind(menus);
	input.right = 1;
	CHECK(mh_menu_main_update(&menus->main, &input, &output,
		&menus->main_choices, NULL) == MH_ERROR_NONE);
	CHECK(menus->main.selected == 3 && output.count == 1);
	CHECK(output.events[0].value == MH_MENU_SOUND_NEXT);
	input.right = 0;
	input.left = 1;
	CHECK(mh_menu_main_update(&menus->main, &input, &output,
		&menus->main_choices, NULL) == MH_ERROR_NONE);
	CHECK(menus->main.selected == 0);

	input.left = 0;
	input.enter = 1;

	for (i = 0; i < MH_MENU_FEATURE_COUNT; i++)
		menus->configuration.items[i] = MH_MENU_HIDDEN;

	CHECK(mh_menu_main_update(&menus->main, &input, &output,
		&menus->main_choices, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 &&
		output.events[0].value == MH_MENU_SOUND_LIMIT);
	CHECK(mh_menu_choice_move(&menus->compact_choices, 0, 11, 1, 1) == 0);
	input.enter = 0;
	input.escape = 1;
	CHECK(mh_menu_options_update(&menus->options, &input, &output,
		&menus->options_choices, NULL) == MH_ERROR_NONE);
	CHECK(output.events[1].value == MH_MENU_MAIN);

	memset(&menus->configuration, 0, sizeof(menus->configuration));
	menus->configuration.items[MH_MENU_FEATURE_LEAGUE] = MH_MENU_HIDDEN;
	menus->single.selected = 1;
	input.escape = 0;
	input.right = 1;
	CHECK(mh_menu_single_update(&menus->single, 0, &input, &output,
		&menus->single_choices, NULL) == MH_ERROR_NONE);
	CHECK(menus->single.selected == 3);
	menus->single.mode = MH_GAME_LEAGUE;
	mh_menus_restrict_mode(menus);
	CHECK(menus->single.mode == MH_GAME_QUICK_RACE);
	menus->configuration.items[MH_MENU_FEATURE_SOUND] = MH_MENU_DISABLED;
	CHECK(mh_menu_choice_move(&menus->compact_choices, 6, 11, 1, 1) == 8);
	CHECK(mh_menu_choice_move(&menus->compact_choices, 10, 11, 1, 1) == 0);
	CHECK(mh_menu_choice_move(&menus->compact_choices, 8, 11, 1, 1) == 9);
	menus->configuration.items[MH_MENU_FEATURE_GRAPHICS] = MH_MENU_HIDDEN;
	CHECK(mh_menu_choice_move(&menus->compact_choices, 6, 11, 1, 1) == 10);

	return 0;
}

static int
disabled_network(struct mh_menus *menus)
{
	struct mh_menu_input input;
	struct mh_menu_output output;
	unsigned int i;

	memset(menus, 0, sizeof(*menus));
	menus->configuration.items[MH_MENU_FEATURE_NETWORK] = MH_MENU_DISABLED;
	mh_menus_configuration_resolve(&menus->configuration,
		mh_content_edition_find("retail-europe"), 0, 1, 1);
	CHECK(menus->configuration.items[MH_MENU_FEATURE_NETWORK] ==
		MH_MENU_DISABLED);
	mh_menus_configuration_bind(menus);
	mh_menus_bind_pages(menus, MH_MENUS_PAGE_MULTI);
	CHECK(menus->menu.current == &menus->main_page);
	mh_menus_request(&menus->menu, &menus->multi_page);
	CHECK(menus->menu.pending == NULL);
	menus->launch_mode = MH_GAME_NETWORK;
	CHECK(!mh_menus_page_enabled(menus, &menus->launch_page));
	menus->single.mode = MH_GAME_NETWORK;
	mh_menus_restrict_mode(menus);
	CHECK(menus->single.mode == MH_GAME_QUICK_RACE);

	memset(&input, 0, sizeof(input));
	input.right = 1;
	CHECK(mh_menu_main_update(&menus->main, &input, &output,
		&menus->main_choices, NULL) == MH_ERROR_NONE);
	CHECK(menus->main.selected == 2);
	input.right = 0;
	input.enter = 1;
	menus->main.selected = 1;
	CHECK(mh_menu_main_update(&menus->main, &input, &output,
		&menus->main_choices, NULL) == MH_ERROR_NONE);

	for (i = 0; i < output.count; i++)
		CHECK(output.events[i].kind != MH_MENU_PAGE ||
			output.events[i].value != MH_MENU_MULTI);

	/* Missing content still wins over a host's disabled label. */
	mh_menus_configuration_resolve(&menus->configuration,
		mh_content_edition_find("motordemo2_soft"), 0, 1, 1);
	CHECK(menus->configuration.items[MH_MENU_FEATURE_NETWORK] ==
		MH_MENU_HIDDEN);

	return 0;
}

static int
feedback(struct mh_menus *menus)
{
	struct mh_control_profile profile;
	struct mh_menu_controls_input input;
	struct mh_menu_controls_output output;
	unsigned int state;

	memset(&profile, 0, sizeof(profile));
	memset(&input, 0, sizeof(input));
	memset(&menus->configuration, 0, sizeof(menus->configuration));
	mh_menus_configuration_bind(menus);
	input.right = 1;

	for (state = MH_MENU_DISABLED; state <= MH_MENU_HIDDEN; state++) {
		memset(&menus->controls, 0, sizeof(menus->controls));
		menus->controls.preferences = 1;
		menus->controls.column = MH_MENU_CONTROLS_BINDINGS;
		menus->configuration.items[MH_MENU_FEATURE_FEEDBACK] =
			(enum mh_menu_availability)state;
		CHECK(mh_menu_controls_update(&menus->controls, &profile, 1,
			&input, &output, &menus->controls_choices,
			NULL) == MH_ERROR_NONE);
		CHECK(menus->controls.row == MH_MENU_CONTROLS_MOUSE_X);
		CHECK(profile.layout.force_feedback == 0 && profile.dirty == 0);
	}

	return 0;
}

static int
labels(struct mh_menus *menus)
{
	struct mh_menu_main_art art;
	struct mh_menu_layout layout;
	struct mh_menu_main_animation animation;
	struct mh_menu_canvas canvas;
	unsigned char pixels[8] = {200, 255, 200, 255, 200, 255, 200, 255};
	unsigned char screen[1024];
	unsigned long seed, noise;
	unsigned int i;

	memset(&art, 0, sizeof(art));
	memset(&layout, 0, sizeof(layout));
	memset(&menus->configuration, 0, sizeof(menus->configuration));
	mh_menus_configuration_bind(menus);
	art.count[7] = 1;
	art.images[7][0].width = art.images[7][0].height = 2;
	art.images[7][0].pixels = pixels;

	for (i = 0; i < 4; i++)
		layout.words[0][30][i] = 1000;

	canvas.pixels = screen;
	canvas.size = sizeof(screen);
	canvas.pitch = 32;
	canvas.begin = 128;
	canvas.end = 640;

	for (i = MH_MENU_ENABLED; i <= MH_MENU_HIDDEN; i++) {
		menus->configuration.items[MH_MENU_FEATURE_NETWORK] =
			(enum mh_menu_availability)i;
		memset(screen, 23, sizeof(screen));
		seed = 123;
		noise = 456;
		CHECK(mh_menu_main_animation_init(&layout, 0, 0, &animation,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_menu_main_render(&art, &layout, 0, 0, 0, 16,
			&animation, &seed, &noise, &canvas,
			&menus->main_choices, NULL) == MH_ERROR_NONE);
		CHECK(screen[128] ==
			(i == MH_MENU_ENABLED			? 200
					: i == MH_MENU_DISABLED ? 3
								: 23));
		CHECK(screen[129] == (i == MH_MENU_ENABLED ? 200 : 23));
	}

	return 0;
}

static int
routing(struct mh_menus *menus)
{
	struct mh_application app;

	memset(menus, 0, sizeof(*menus));
	menus->configuration.items[MH_MENU_FEATURE_NETWORK] = MH_MENU_HIDDEN;
	menus->configuration.items[MH_MENU_FEATURE_LEAGUE] = MH_MENU_DISABLED;
	mh_menus_bind_pages(menus, MH_MENUS_PAGE_MULTI);
	CHECK(menus->menu.current == &menus->main_page);
	mh_menus_request(&menus->menu, &menus->multi_page);
	CHECK(menus->menu.pending == NULL && menus->menu.leaving == 0);
	CHECK(!mh_menus_page_enabled(menus, &menus->league_page[0]));
	menus->launch_mode = MH_GAME_NETWORK;
	CHECK(!mh_menus_page_enabled(menus, &menus->launch_page));
	menus->configuration.items[MH_MENU_FEATURE_RACE] = MH_MENU_HIDDEN;
	CHECK(!mh_menus_page_enabled(menus, &menus->race_page));
	menus->single.mode = MH_GAME_TIME_ATTACK;
	CHECK(mh_menus_page_enabled(menus, &menus->race_page));
	menus->menu.current = &menus->launch_page;
	mh_menus_launch_input(&menus->menu, menus);
	CHECK(!menus->launch_requested);
	mh_menus_request(&menus->menu, &menus->options_page);
	CHECK(menus->menu.pending == &menus->options_page);

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_menu_restrict(&app, MH_MENU_FEATURE_NETWORK,
		MH_MENU_HIDDEN, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_menu_restrict(&app, MH_MENU_FEATURE_NETWORK,
		MH_MENU_ENABLED, NULL) == MH_ERROR_NONE);
	CHECK(app.menu_configuration.items[MH_MENU_FEATURE_NETWORK] ==
		MH_MENU_HIDDEN);
	app.phase = APPLICATION_MENU;
	CHECK(mh_application_menu_restrict(&app, MH_MENU_FEATURE_SOUND,
		MH_MENU_DISABLED, NULL) == MH_ERROR_STATE);

	return 0;
}

/* Unlocks affect the menu catalog, not the supplied saved progress flags. */
static int
unlocks(struct mh_menus *menus)
{
	static const unsigned long divisions[MH_TRACK_CATALOG_LIMIT] = {
		100, 101, 200, 201, 300, 301, 310, 400};

	static const unsigned long cars[] = {
		100, 100, 100, 200, 200, 200, 300, 300, 300, 400};

	unsigned long flags[MH_TRACK_CATALOG_LIMIT];
	unsigned int i;

	memset(menus, 0, sizeof(*menus));
	memset(flags, 0, sizeof(flags));
	menus->track_catalog.count = 8;
	menus->car_catalog.count = 10;
	memcpy(menus->car_divisions, cars, sizeof(cars));
	CHECK(mh_menus_unlocks_apply(menus, divisions, flags, NULL) ==
		MH_ERROR_NONE);
	CHECK(menus->track_unlocks.unlocked == 2);
	CHECK(menus->car_unlocks.unlocked == 3);
	menus->unlock_cheat = 1;
	CHECK(mh_menus_unlocks_apply(menus, divisions, flags, NULL) ==
		MH_ERROR_NONE);
	CHECK(menus->track_unlocks.unlocked == 8);
	CHECK(menus->car_unlocks.unlocked == 10);
	CHECK(menus->availability == 255);

	for (i = 0; i < MH_TRACK_CATALOG_LIMIT; i++)
		CHECK(flags[i] == 0);

	menus->unlock_cheat = 0;
	CHECK(mh_menus_unlocks_apply(menus, divisions, flags, NULL) ==
		MH_ERROR_NONE);
	CHECK(menus->track_unlocks.unlocked == 2);
	CHECK(menus->car_unlocks.unlocked == 3);

	return 0;
}

int
main(void)
{
	struct mh_menus *menus;
	int result;

	menus = calloc(1, sizeof(*menus));
	CHECK(menus != NULL);
	result = resolution() || disabled_network(menus) || navigation(menus) ||
		feedback(menus) || labels(menus) || routing(menus) ||
		unlocks(menus);
	free(menus);

	return result;
}

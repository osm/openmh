#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/load.h"
#include "engine/core/arguments.h"
#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/network/endpoint.h"
#include "engine/network/protocol_profile.h"
#include "engine/simulation/tick_clock.h"
#include "engine/ui/menu_controls.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_graphics.h"

enum {
	FRAME_COUNT_MAX = 100000,
	CAPTURE_STEP_MAX_MS = 1000,
	CAPTURE_DEFAULT_FRAMES = 120,
	CAPTURE_DEFAULT_STEP_MS = 16
};

#ifdef MH_DEBUG
struct page_option {
	const char			*name;
	enum mh_menus_start_page	page;
};
#endif

static int
parse_toggle(const char *text, unsigned int *output)
{
	if (strcmp(text, "0") != 0 && strcmp(text, "1") != 0)
		return 0;

	*output = text[0] == '1';

	return 1;
}

#ifdef MH_DEBUG
static int
parse_page(const char *name, enum mh_menus_start_page *page)
{
	static const struct page_option pages[] = {
		{"main", MH_MENUS_PAGE_MAIN},
		{"single", MH_MENUS_PAGE_SINGLE},
		{"setup", MH_MENUS_PAGE_SETUP},
		{"car", MH_MENUS_PAGE_CAR},
		{"ghost", MH_MENUS_PAGE_GHOST},
		{"league", MH_MENUS_PAGE_LEAGUE},
		{"standings", MH_MENUS_PAGE_STANDINGS},
		{"league-load", MH_MENUS_PAGE_LEAGUE_LOAD},
		{"league-new", MH_MENUS_PAGE_LEAGUE_NEW},
		{"league-delete", MH_MENUS_PAGE_LEAGUE_DELETE},
		{"options", MH_MENUS_PAGE_OPTIONS},
		{"sound", MH_MENUS_PAGE_SOUND},
		{"graphics", MH_MENUS_PAGE_GRAPHICS},
		{"personal", MH_MENUS_PAGE_PERSONAL},
		{"controls", MH_MENUS_PAGE_CONTROLS},
		{"multi", MH_MENUS_PAGE_MULTI},
		{"credits", MH_MENUS_PAGE_CREDITS},
		{"rankings", MH_MENUS_PAGE_RANKINGS},
	};

	unsigned int i;

	for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
		if (strcmp(name, pages[i].name) == 0) {
			*page = pages[i].page;
			return 1;
		}
	}

	return 0;
}

static int
parse_debug_menu_option(
	struct mh_application *app, const char *option, const char *value)
{
	if (strcmp(option, "--debug-selection") == 0)
		return mh_arguments_unsigned(value, MH_MENU_GRAPHICS_ROWS - 1,
			&app->start_selection);

	if (strcmp(option, "--debug-seed") == 0)
		return mh_arguments_unsigned(
			value, MH_U32_MAX, &app->initial_seed);

	if (strcmp(option, "--debug-frames") == 0)
		return mh_arguments_unsigned(
			value, FRAME_COUNT_MAX, &app->frame_limit) &&
			app->frame_limit != 0;

	if (strcmp(option, "--debug-step") == 0)
		return mh_arguments_unsigned(value, CAPTURE_STEP_MAX_MS,
			&app->capture_step_ms) &&
			app->capture_step_ms != 0;

	if (strcmp(option, "--debug-unlocked-tracks") == 0)
		return mh_arguments_unsigned(value, MH_TRACK_CATALOG_LIMIT,
			&app->unlocked_tracks) &&
			app->unlocked_tracks != 0;

	if (strcmp(option, "--debug-unlocked-cars") == 0)
		return mh_arguments_unsigned(value, MH_CAR_CATALOG_LIMIT,
			&app->unlocked_cars) &&
			app->unlocked_cars != 0;

	if (strcmp(option, "--debug-special-cars") == 0)
		return mh_arguments_unsigned(
			value, 1, &app->special_cars_override);

	if (strcmp(option, "--debug-stage") == 0)
		return mh_arguments_unsigned(value, 3, &app->stage);

	if (strcmp(option, "--debug-capture") == 0)
		app->capture_path = value;
	else if (strcmp(option, "--debug-indexed") == 0)
		app->indexed_path = value;
	else if (strcmp(option, "--debug-page") == 0 &&
		parse_page(value, &app->start_page))
		app->no_intro = 1;
	else if (strcmp(option, "--debug-dialog") == 0 &&
		(strcmp(value, "exit") == 0 || strcmp(value, "blocked") == 0))
		app->startup_dialog = strcmp(value, "exit") == 0
			? MH_MENU_CONFIRM_EXIT
			: MH_MENU_BLOCK_EXIT;
	else
		return 0;

	return 1;
}

static int
parse_debug_race_option(
	struct mh_application *app, const char *option, const char *value)
{
	if (strcmp(option, "--debug-name") == 0)
		app->player_name = value;

	else if (strcmp(option, "--debug-team") == 0)
		app->team_name = value;
	else if (strcmp(option, "--debug-horn") == 0)
		app->horn_name = value;
	else if (strcmp(option, "--debug-confirmation") == 0 &&
		mh_arguments_unsigned(value, 2, &app->menu_confirmation)) {
		app->overrides |= MH_APP_OVERRIDE_CONFIRMATION;
	} else if (strcmp(option, "--debug-mode") == 0 &&
		mh_arguments_unsigned(value, 3, &app->game_type)) {
		app->overrides |= MH_APP_OVERRIDE_MODE;
	} else if (strcmp(option, "--debug-car") == 0 &&
		mh_arguments_unsigned(
			value, MH_CAR_CATALOG_LIMIT, &app->car_number) &&
		app->car_number != 0) {
		app->overrides |= MH_APP_OVERRIDE_CAR;
	} else if (strcmp(option, "--debug-track") == 0 &&
		mh_arguments_unsigned(
			value, MH_TRACK_CATALOG_LIMIT, &app->track_number) &&
		app->track_number != 0) {
		app->overrides |= MH_APP_OVERRIDE_TRACK;
	} else if (strcmp(option, "--debug-laps") == 0 &&
		mh_arguments_unsigned(value, MH_RACE_MAX_LAPS, &app->laps) &&
		app->laps >= MH_RACE_MIN_LAPS) {
		app->overrides |= MH_APP_OVERRIDE_LAPS;
	} else if (strcmp(option, "--debug-catch-up") == 0 &&
		mh_arguments_unsigned(value, 1, &app->catch_up)) {
		app->overrides |= MH_APP_OVERRIDE_CATCH_UP;
	} else if (strcmp(option, "--debug-automatic") == 0 &&
		mh_arguments_unsigned(value, 1, &app->automatic)) {
		app->overrides |= MH_APP_OVERRIDE_AUTOMATIC;
	} else if (strcmp(option, "--debug-record") == 0 &&
		mh_arguments_unsigned(value, 1, &app->record_race)) {
		app->overrides |= MH_APP_OVERRIDE_RECORD;
	} else
		return 0;

	return 1;
}

static int
parse_debug_data_option(
	struct mh_application *app, const char *option, const char *value)
{
	if (strcmp(option, "--debug-no-settings") == 0) {
		app->no_settings = 1;
		return 1;
	}

	if (strcmp(option, "--debug-no-records") == 0) {
		app->session_options.no_records = 1;
		return 1;
	}

	if (value == NULL)
		return 0;

	if (strcmp(option, "--debug-settings") == 0)
		app->settings_path = value;
	else if (strcmp(option, "--debug-controls") == 0)
		app->controls_directory = value;
	else if (strcmp(option, "--debug-progress") == 0)
		app->progress_directory = value;
	else if (strcmp(option, "--debug-leagues") == 0)
		app->league_directory = value;
	else if (strcmp(option, "--debug-records") == 0)
		app->session_options.record_directory = value;
	else if (strcmp(option, "--debug-recordings") == 0)
		app->session_options.recording_directory = value;
	else if (strcmp(option, "--debug-addresses") == 0)
		app->addresses_directory = value;
	else
		return 0;

	return 2;
}

/* Return the number of arguments consumed, or zero for an invalid option. */
static int
parse_debug_option(
	struct mh_application *app, const char *option, const char *value)
{
	int consumed;

	if (strcmp(option, "--debug-no-intro") == 0) {
		app->no_intro = 1;
		return 1;
	}

	if (strcmp(option, "--debug-require-audio") == 0) {
		app->audio_mode = MH_MENUS_AUDIO_REQUIRED;
		return 1;
	}

	consumed = mh_debug_parse_option(
		&app->session_options.debug, option, value);

	if (consumed != 0)
		return consumed;

	consumed = parse_debug_data_option(app, option, value);

	if (consumed != 0)
		return consumed;

	if (value == NULL)
		return 0;

	if (parse_debug_race_option(app, option, value) ||
		parse_debug_menu_option(app, option, value))
		return 2;

	return 0;
}

#endif

static void
set_defaults(struct mh_application *app)
{
	app->asset_root =
		app->game_directory[0] != 0 ? app->game_directory : ".";
	app->assets_explicit = 0;
	app->user_data_directory = NULL;
	app->capture_path = app->indexed_path = app->horn_name =
		app->progress_directory = NULL;
	app->settings_path = app->player_name = app->team_name =
		app->league_directory = NULL;
	app->settings_store = NULL;
	app->preferences = NULL;
	app->controls_directory = app->addresses_directory = NULL;
	app->connect_address = NULL;
	app->no_settings = app->no_intro = 0;
	app->overrides = 0;
	app->fullscreen = app->show_fps = 0;
	app->vsync = 1;
	app->max_fps = 0;
	app->grab_mouse = 1;
	memset(&app->session_options, 0, sizeof(app->session_options));
	app->session_options.network_wire = MH_NET_WIRE_31_LOBBY;
	app->session_options.simulation_rate = MH_SIM_RATE_DEFAULT;
	app->session_options.network_legacy =
		mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY;
	memset(&app->score_history, 0, sizeof(app->score_history));
	app->session_options.score_history = &app->score_history;
	app->frame_limit = 0;
	app->start_selection = 0;
	app->initial_seed = 1;
	app->capture_step_ms = CAPTURE_DEFAULT_STEP_MS;
	app->audio_mode = MH_MENUS_AUDIO_AUTO;
	app->menu_confirmation = 1;
	app->startup_dialog = 0;
	app->game_type = 0;
	app->car_number = app->track_number = 1;
	app->start_page = MH_MENUS_PAGE_MAIN;
	app->laps = 5;
	app->catch_up = 0;
	app->unlock_all = 0;
	app->unlocked_tracks = 0;
	app->unlocked_cars = 0;
	app->special_cars_override = 2;
	app->automatic = 1;
	app->record_race = 0;
	app->stage = 3;
	app->renderer = MH_RENDER_CHOICE_COUNT;
}

static void
print_usage(FILE *output)
{
	static const char *const general[] = {("OpenMH " MH_BUILD_VERSION "\n"),
		"\n", "Usage: openmotor [options]\n", "\n", "Options\n",
		"  --help                           Show this help and exit.\n",
		"\n", "Game and resources\n",
		"  --assets PATH                    Game directory or .ass (default: beside\n",
		"                                   openmotor).\n",
		"  --user-data DIR                  Settings and saves directory (default:\n",
		"                                   userdata inside the game directory or\n",
		"                                   beside the .ass package).\n",
		"  --unlock-all                     Unlock installed cars/tracks for this run.\n",
		"  --sim-rate HZ                    Simulation Hz (default: 120).\n",
		"\n", "Display and input\n"};

	static const char *const display[] = {
		"  --fullscreen 0|1                 Windowed (0) or borderless fullscreen (1).\n",
		"  --grab-mouse 0|1                 Release (0) or capture (1) the mouse in a\n",
		"                                   window.\n",
		"  --vsync 0|1                      VSync (default: on).\n",
		"  --max-fps 0..10000               Render limit; 0 is uncapped (default).\n",
		"  --show-fps 0|1                   Hide (0) or show (1) the race FPS counter.\n",
		"\n", "Audio\n",
		"  --audio 0|1                      Disable/enable all audio (default: 1).\n"};

#ifdef MH_DEBUG
	static const char *const debug[] = {
		"\n",
		"Development controls (MH_DEBUG=ON)\n",
		"\n",
		"Development: direct sessions (selector must be first)\n",
		"  --debug-race                     Start a diagnostic race.\n",
		"  --debug-demo FILE                Play or race a recording.\n",
		"  --debug-league                   Create or resume a league.\n",
		"  --debug-network                  Host or join a network race.\n",
		"\n",
		"  Use openmotor --debug-race --help for that session's controls.\n",
		"  League/network availability depends on the build.\n",
		"\n",
		"Development: player and race settings\n",
		"  --debug-require-audio            Require a working menu audio device.\n",
		"  --debug-name NAME                Player name.\n",
		"  --debug-team TEAM                Team name (up to four characters).\n",
		"  --debug-horn NAME                Horn selection.\n",
		"  --debug-mode 0..3                0 Quick Race, 1 League, 2 Time Attack, 3\n",
		"                                   Ghost.\n",
		"  --debug-car NUMBER               Car source-file number.\n",
		"  --debug-track NUMBER             Track source-file number.\n",
		"  --debug-laps COUNT               Number of laps.\n",
		"  --debug-catch-up 0|1             Disable/enable catch-up.\n",
		"  --debug-automatic 0|1            Manual/automatic transmission.\n",
		"  --debug-record 0|1               Disable/enable race recording.\n",
		"  --debug-confirmation 0..2        0 Off, 1 On, 2 NoExit.\n",
		"\n",
		"  These override saved settings; start the race from the menus.\n",
		"\n",
		"Development: settings and saved data\n",
		"  --debug-settings FILE            Use a different settings file.\n",
		"  --debug-no-settings              Disable settings/default controls\n",
		"                                   persistence.\n",
		"  --debug-controls DIR             Control-profile directory.\n",
		"  --debug-progress DIR             Progression/unlock directory.\n",
		"  --debug-leagues DIR              League-save directory.\n",
		"  --debug-records DIR              High-score directory.\n",
		"  --debug-no-records               Disable normal high-score loading/saving.\n",
		"  --debug-recordings DIR           Race-recording directory.\n",
		"  --debug-addresses DIR            Multiplayer address-book directory.\n",
		"\n",
		"  --debug-settings conflicts with --debug-no-settings.\n",
		"  --debug-records conflicts with --debug-no-records.\n",
		"\n",
		"Development: menus and captures\n",
		"  --debug-no-intro                 Skip the opening videos.\n",
		"  --debug-page PAGE                Open a menu page directly; skip the intro.\n",
		"  --debug-selection N              Initial menu item (range depends on page).\n",
		"  --debug-dialog exit|blocked      Show a main-menu dialog.\n",
		"  --debug-capture FILE.bmp         Save a menu capture; do not save settings.\n",
		"  --debug-indexed FILE             Also export indexed pixels; needs\n",
		"                                   --debug-capture.\n",
		"  --debug-frames 1..100000         Limit the number of menu frames.\n",
		"  --debug-step 1..1000             Milliseconds per captured menu frame.\n",
		"  --debug-seed N                   Initial random seed (0..4294967295).\n",
		"  --debug-unlocked-cars COUNT      Override the unlocked-car count.\n",
		"  --debug-unlocked-tracks COUNT    Override the unlocked-track count.\n",
		"  --debug-special-cars 0|1         Override special-car availability.\n",
		"  --debug-stage 0..3               Override the race-setup stage.\n",
		"\n",
		"  Pages: main, single, setup, car, ghost, league, standings,\n",
		"         league-load, league-new, league-delete, options, sound,\n",
		"         graphics, personal, controls, multi, credits, rankings.\n",
		"\n",
		"  Captures default to 120 frames at 16 ms per frame.\n",
	};
#endif
	const char *separator;
	unsigned int i;
#ifdef MH_CLIENT_NETWORK
	const struct mh_net_protocol *legacy, *openmh;
#endif

	for (i = 0; i < sizeof(general) / sizeof(general[0]); i++)
		fputs(general[i], output);

	fputs("  --renderer NAME                  ", output);
	separator = "";

	for (i = 0; i < MH_RENDER_CHOICE_COUNT; i++)
		if (mh_render_choice_available(i)) {
			fprintf(output, "%s%s", separator,
				mh_render_choice_option(i));
			separator = ", ";
		}

	fputs(".\n", output);

	for (i = 0; i < sizeof(display) / sizeof(display[0]); i++)
		fputs(display[i], output);

#ifdef MH_CLIENT_NETWORK
	legacy = mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY);
	openmh = mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH);
	fputs("\nNetworking\n", output);
	fprintf(output,
		"  --connect HOST[:PORT]            Join a server (default port: %u).\n",
		(unsigned int)MH_NET_LANINET_PORT);
	fprintf(output,
		"  --network-protocol NAME          %s%s%s (default: %s).\n",
		legacy ? legacy->name : "", legacy && openmh ? ", " : "",
		openmh ? openmh->name : "",
		mh_net_protocol_get(mh_net_protocol_default())->name);

#endif
#ifdef MH_DEBUG
	for (i = 0; i < sizeof(debug) / sizeof(debug[0]); i++)
		fputs(debug[i], output);

	mh_debug_print_usage(output);
#endif
	fputs("\nExamples\n"
	"  openmotor\n"
	"  openmotor --assets /path/to/Motorhead --fullscreen 1\n"
	"  openmotor --fullscreen 0 --grab-mouse 0\n"
	"\n",
		output);
}

static enum mh_argument_kind
argument_kind(const char *name)
{
	static const struct mh_argument_option options[] = {
		{"--network-protocol", MH_ARGUMENT_VALUE},
		{"--connect", MH_ARGUMENT_VALUE},
		{"--sim-rate", MH_ARGUMENT_VALUE},
		{"--assets", MH_ARGUMENT_VALUE},
		{"--user-data", MH_ARGUMENT_VALUE},
		{"--renderer", MH_ARGUMENT_VALUE},
		{"--fullscreen", MH_ARGUMENT_VALUE},
		{"--grab-mouse", MH_ARGUMENT_VALUE},
		{"--show-fps", MH_ARGUMENT_VALUE},
		{"--vsync", MH_ARGUMENT_VALUE},
		{"--max-fps", MH_ARGUMENT_VALUE},
		{"--unlock-all", MH_ARGUMENT_FLAG},
		{"--audio", MH_ARGUMENT_VALUE}, {"--help", MH_ARGUMENT_FLAG},
#ifdef MH_DEBUG
		{"--debug-selection", MH_ARGUMENT_VALUE},
		{"--debug-seed", MH_ARGUMENT_VALUE},
		{"--debug-frames", MH_ARGUMENT_VALUE},
		{"--debug-step", MH_ARGUMENT_VALUE},
		{"--debug-unlocked-tracks", MH_ARGUMENT_VALUE},
		{"--debug-unlocked-cars", MH_ARGUMENT_VALUE},
		{"--debug-special-cars", MH_ARGUMENT_VALUE},
		{"--debug-stage", MH_ARGUMENT_VALUE},
		{"--debug-capture", MH_ARGUMENT_VALUE},
		{"--debug-indexed", MH_ARGUMENT_VALUE},
		{"--debug-page", MH_ARGUMENT_VALUE},
		{"--debug-dialog", MH_ARGUMENT_VALUE},
		{"--debug-name", MH_ARGUMENT_VALUE},
		{"--debug-team", MH_ARGUMENT_VALUE},
		{"--debug-horn", MH_ARGUMENT_VALUE},
		{"--debug-confirmation", MH_ARGUMENT_VALUE},
		{"--debug-mode", MH_ARGUMENT_VALUE},
		{"--debug-car", MH_ARGUMENT_VALUE},
		{"--debug-track", MH_ARGUMENT_VALUE},
		{"--debug-laps", MH_ARGUMENT_VALUE},
		{"--debug-catch-up", MH_ARGUMENT_VALUE},
		{"--debug-automatic", MH_ARGUMENT_VALUE},
		{"--debug-record", MH_ARGUMENT_VALUE},
		{"--debug-no-settings", MH_ARGUMENT_FLAG},
		{"--debug-no-records", MH_ARGUMENT_FLAG},
		{"--debug-settings", MH_ARGUMENT_VALUE},
		{"--debug-controls", MH_ARGUMENT_VALUE},
		{"--debug-progress", MH_ARGUMENT_VALUE},
		{"--debug-leagues", MH_ARGUMENT_VALUE},
		{"--debug-records", MH_ARGUMENT_VALUE},
		{"--debug-recordings", MH_ARGUMENT_VALUE},
		{"--debug-addresses", MH_ARGUMENT_VALUE},
		{"--debug-no-intro", MH_ARGUMENT_FLAG},
		{"--debug-require-audio", MH_ARGUMENT_FLAG},
#endif
		{NULL, MH_ARGUMENT_UNKNOWN}};

	enum mh_argument_kind kind;

	kind = mh_arguments_find(name, options);

#ifdef MH_DEBUG
	if (kind == MH_ARGUMENT_UNKNOWN)
		kind = mh_debug_argument_kind(name);
#endif

	return kind;
}

static int
parse_options(struct mh_application *app, int argc, char *argv[])
{
	struct mh_arguments arguments;
	struct mh_network_endpoint endpoint;
	enum mh_argument_result result;
	const char *option, *value;
	unsigned long rate;
	unsigned int audio;

	mh_arguments_init(&arguments, argc, argv);

	while ((result = mh_arguments_next(&arguments, argument_kind)) !=
		MH_ARGUMENT_END) {
		option = arguments.argument;
		value = arguments.value;

		if (result == MH_ARGUMENT_ERROR ||
			result == MH_ARGUMENT_POSITIONAL) {
			fprintf(stderr, "openmotor: %s: %s\n",
				result == MH_ARGUMENT_ERROR
					? arguments.message
					: "unexpected positional argument",
				option == NULL ? "" : option);
			return 1;
		}

		if (strcmp(option, "--help") == 0) {
			print_usage(stdout);
			return 0;
		}

		if (strcmp(option, "--network-protocol") == 0) {
			if (strcmp(value, "openmh") != 0 &&
				strcmp(value, "legacy") != 0) {
				fprintf(stderr,
					"Network protocol must be legacy or openmh.\n");
				return 1;
			}

			if (mh_net_protocol_get(!strcmp(value, "legacy")
					? MH_NET_PROTOCOL_LEGACY
					: MH_NET_PROTOCOL_OPENMH) == NULL) {
				fprintf(stderr,
					"Requested network protocol is not included in this build.\n");
				return 1;
			}

			app->session_options.network_legacy =
				!strcmp(value, "legacy");
		} else if (strcmp(option, "--sim-rate") == 0) {
			if (!mh_arguments_unsigned(
				value, MH_SIM_RATE_MAX, &rate) ||
				!mh_sim_rate_valid(rate)) {
				fprintf(stderr,
					"Simulation rate must be an integer from 1 to 4294967295 Hz.\n");
				return 1;
			}

			app->session_options.simulation_rate =
				(unsigned int)rate;
		} else if (strcmp(option, "--connect") == 0 &&
			mh_network_endpoint_parse(value, MH_NET_LANINET_PORT,
				&endpoint, NULL) == MH_ERROR_NONE)
			app->connect_address = value;
		else if (strcmp(option, "--assets") == 0) {
			app->asset_root = value;
			app->assets_explicit = 1;
		} else if (strcmp(option, "--user-data") == 0)
			app->user_data_directory = value;
		else if (strcmp(option, "--renderer") == 0 &&
			mh_render_choice_parse(value, &app->renderer))
			continue;
		else if (strcmp(option, "--fullscreen") == 0 &&
			parse_toggle(value, &app->fullscreen))
			app->overrides |= MH_APP_OVERRIDE_FULLSCREEN;
		else if (strcmp(option, "--grab-mouse") == 0 &&
			parse_toggle(value, &app->grab_mouse))
			app->overrides |= MH_APP_OVERRIDE_GRAB_MOUSE;
		else if (strcmp(option, "--show-fps") == 0 &&
			parse_toggle(value, &app->show_fps))
			app->overrides |= MH_APP_OVERRIDE_SHOW_FPS;
		else if (strcmp(option, "--vsync") == 0 &&
			parse_toggle(value, &app->vsync))
			app->overrides |= MH_APP_OVERRIDE_VSYNC;
		else if (strcmp(option, "--max-fps") == 0 &&
			mh_arguments_unsigned(
				value, MH_AV_MAX_FPS, &app->max_fps))
			app->overrides |= MH_APP_OVERRIDE_MAX_FPS;
		else if (strcmp(option, "--unlock-all") == 0)
			app->unlock_all = 1;
		else if (strcmp(option, "--audio") == 0 &&
			parse_toggle(value, &audio))
			app->audio_mode = audio ? MH_MENUS_AUDIO_AUTO
						: MH_MENUS_AUDIO_DISABLED;
#ifdef MH_DEBUG
		else if (parse_debug_option(app, option, value) != 0)
			continue;
#endif
		else {
			fprintf(stderr,
				"openmotor: invalid value for %s: %s\n"
				"Try 'openmotor --help' for usage.\n",
				option, value == NULL ? "" : value);
			return 1;
		}
	}

	return -1;
}

#ifdef MH_DEBUG
static int
validate_selection(const struct mh_application *app)
{
	if (app->start_page != MH_MENUS_PAGE_MAIN && app->startup_dialog != 0) {
		fprintf(stderr, "--debug-dialog requires the main page\n");
		return 2;
	}

	if (app->start_page != MH_MENUS_PAGE_GRAPHICS &&
		app->start_page != MH_MENUS_PAGE_PERSONAL &&
		app->start_page != MH_MENUS_PAGE_CONTROLS &&
		app->start_selection > 4) {
		fprintf(stderr,
			"Selection above 4 requires Graphics, Personal or Controls\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_PERSONAL &&
		app->start_selection > 17) {
		fprintf(stderr,
			"Personal selection must be 0..17 (row 17 needs patched assets)\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_CONTROLS &&
		app->start_selection >= MH_MENU_CONTROLS_ROWS) {
		fprintf(stderr,
			"Controls row must be 0..13 (rows 10..13 need patched assets)\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_SETUP &&
		app->start_selection > 2) {
		fprintf(stderr, "Setup selection must be 0..2\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_CAR && app->start_selection > 3) {
		fprintf(stderr, "Car selection must be 0..3\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_GHOST &&
		app->start_selection > 2) {
		fprintf(stderr, "Ghost focus must be 0..2\n");
		return 2;
	}

	if (app->start_page == MH_MENUS_PAGE_SOUND &&
		app->start_selection > 4) {
		fprintf(stderr, "Sound selection must be 0..4\n");
		return 2;
	}

	return -1;
}

static int
validate_options(struct mh_application *app)
{
	unsigned int i;

	int result;

	if (app->no_settings && app->settings_path != NULL) {
		fprintf(stderr,
			"--debug-settings conflicts with --debug-no-settings\n");
		return 2;
	}

	if (app->session_options.no_records &&
		app->session_options.record_directory != NULL) {
		fprintf(stderr,
			"--debug-records conflicts with --debug-no-records\n");
		return 2;
	}

	if (app->team_name != NULL) {
		for (i = 0; app->team_name[i] != 0; i++)
			if (i == MH_SETTINGS_TEAM_BYTES - 1 ||
				(unsigned char)app->team_name[i] < 32 ||
				(unsigned char)app->team_name[i] > 126) {
				fprintf(stderr,
					"Team must contain at most four printable ASCII characters\n");
				return 2;
			}
	}

	if (app->indexed_path != NULL && app->capture_path == NULL) {
		fprintf(stderr, "--debug-indexed requires --debug-capture\n");
		return 2;
	}

	result = validate_selection(app);

	if (result >= 0)
		return result;

	if (app->capture_path != NULL && app->frame_limit == 0)
		app->frame_limit = CAPTURE_DEFAULT_FRAMES;

	return -1;
}

#endif

int
mh_application_parse_arguments(
	struct mh_application *app, int argc, char *argv[])
{
	int result;

	set_defaults(app);
	result = parse_options(app, argc, argv);

	if (result >= 0)
		return result;

	if (app->connect_address != NULL) {
		app->no_intro = 1;
		app->start_page = MH_MENUS_PAGE_MULTI;
	}

#ifdef MH_DEBUG
	return validate_options(app);
#else

	return -1;
#endif
}

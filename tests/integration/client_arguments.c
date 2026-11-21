#include <stdio.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/settings/render_choice.h"
#include "engine/network/protocol_profile.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "client arguments:%d: %s\n", __LINE__, \
				#condition);                                   \
			return 1;                                              \
		}                                                              \
	} while (0)

struct simulation_rate_case {
	char		*text;
	unsigned long	rate;
};

static int
check_debug_options(void)
{
	static const char *const options[][2] = {{"--debug-page", "credits"},
		{"--debug-selection", "4"}, {"--debug-dialog", "exit"},
		{"--debug-capture", "menu.bmp"},
		{"--debug-indexed", "menu.idx"}, {"--debug-frames", "2"},
		{"--debug-step", "16"}, {"--debug-seed", "7"},
		{"--debug-unlocked-cars", "1"},
		{"--debug-unlocked-tracks", "1"}, {"--debug-special-cars", "1"},
		{"--debug-stage", "2"}, {"--debug-name", "Driver"},
		{"--debug-team", "TEST"}, {"--debug-horn", "Honker1"},
		{"--debug-mode", "1"}, {"--debug-car", "2"},
		{"--debug-track", "3"}, {"--debug-laps", "4"},
		{"--debug-catch-up", "1"}, {"--debug-automatic", "0"},
		{"--debug-record", "1"}, {"--debug-confirmation", "2"},
		{"--debug-settings", "settings.cfg"},
		{"--debug-controls", "controls"},
		{"--debug-progress", "progress"},
		{"--debug-leagues", "leagues"}, {"--debug-records", "records"},
		{"--debug-recordings", "recordings"},
		{"--debug-addresses", "addresses"}};

	struct mh_application app;
	char *args[] = {"openmotor", NULL, NULL, "--debug-capture", "menu.bmp"};
	char legacy[64];
	unsigned int i;
	int result, count;

	for (i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		memset(&app, 0, sizeof(app));
		args[1] = (char *)options[i][0];
		args[2] = (char *)options[i][1];
		count = strcmp(args[1], "--debug-indexed") == 0 ? 5 : 3;
		result = mh_application_parse_arguments(&app, count, args);
#ifdef MH_DEBUG
		CHECK(result == -1);
#else
		CHECK(result == 1);
#endif
		CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
		sprintf(legacy, "--%s", options[i][0] + 8);
		args[1] = legacy;
		CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	}

	return 0;
}

static int
check_shared_syntax(void)
{
	struct mh_application app;
	char *args[] = {
		"openmotor", "--assets", "first", "--assets", "last", "--"};

	char *bad[][3] = {{"openmotor", "--assets", "--unlock-all"},
		{"openmotor", "--assets", ""}, {"openmotor", "--", "--help"},
		{"openmotor", "--unknown", "value"},
		{"openmotor", "file", "--help"}};

	size_t i;

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 6, args) == -1);
	CHECK(strcmp(app.asset_root, "last") == 0);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(mh_application_parse_arguments(&app, 3, bad[i]) == 1);

	return 0;
}

static int
check_connect_option(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--connect", "localhost:16666",
		"--network-protocol", "legacy"};

	char *repeated[] = {
		"openmotor", "--connect", "first", "--connect", "second"};

	char *missing[] = {"openmotor", "--connect", "--unlock-all"};
	char *bad[] = {"openmotor", "--connect", "localhost:0"};

	args[4] = (char *)mh_net_protocol_get(mh_net_protocol_default())->name;
	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 5, args) == -1);
	CHECK(strcmp(app.connect_address, "localhost:16666") == 0);
	CHECK(app.session_options.network_legacy ==
		(mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY));
	CHECK(app.no_intro && app.start_page == MH_MENUS_PAGE_MULTI);
	CHECK(mh_application_parse_arguments(&app, 5, repeated) == -1);
	CHECK(strcmp(app.connect_address, "second") == 0);
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	CHECK(app.connect_address == NULL && !app.no_intro);
	CHECK(app.start_page == MH_MENUS_PAGE_MAIN);
	CHECK(mh_application_parse_arguments(&app, 2, missing) == 1);
	CHECK(mh_application_parse_arguments(&app, 3, missing) == 1);
	CHECK(mh_application_parse_arguments(&app, 3, bad) == 1);
	bad[2] = "localhost:65536";
	CHECK(mh_application_parse_arguments(&app, 3, bad) == 1);
	bad[2] = "localhost:";
	CHECK(mh_application_parse_arguments(&app, 3, bad) == 1);
	missing[2] = "";
	CHECK(mh_application_parse_arguments(&app, 3, missing) == 1);

	return 0;
}

static int
check_player_options(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--assets", "game", "--renderer",
		"software", "--fullscreen", "1", "--grab-mouse", "0",
		"--show-fps", "1", "--unlock-all", "--sim-rate", "240",
		"--network-protocol", "legacy", "--vsync", "1", "--max-fps",
		"1001"};

	char *invalid[] = {"openmotor", "--network-port", "0"};

	args[15] = (char *)mh_net_protocol_get(mh_net_protocol_default())->name;
	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(
		&app, (int)(sizeof(args) / sizeof(args[0])), args) == -1);
	CHECK(strcmp(app.asset_root, "game") == 0);
	CHECK(app.fullscreen && !app.grab_mouse && app.show_fps &&
		!app.no_intro);
	CHECK(app.vsync == 1 && app.max_fps == 1001);
	CHECK((app.overrides & MH_APP_OVERRIDE_VSYNC) &&
		(app.overrides & MH_APP_OVERRIDE_MAX_FPS));
	CHECK(app.unlock_all);
	CHECK(app.session_options.simulation_rate == 240 &&
		app.session_options.network_legacy ==
			(mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY));
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	CHECK(!app.unlock_all);
	CHECK(app.vsync == 1 && app.max_fps == 0);
	CHECK(app.session_options.simulation_rate == 120 &&
		app.session_options.network_legacy ==
			(mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY));
	CHECK(app.player_name == NULL && app.settings_path == NULL);
	CHECK(!app.no_settings && !app.session_options.no_records);
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);
	invalid[1] = "--vsync";
	invalid[2] = "0";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == -1);
	CHECK(app.vsync == 0 && (app.overrides & MH_APP_OVERRIDE_VSYNC));
	invalid[2] = "2";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);
	invalid[1] = "--max-fps";
	invalid[2] = "10001";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);
	invalid[2] = "-1";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);
	invalid[2] = "200.5";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);
	invalid[2] = "0";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == -1);
	invalid[1] = "--network-protocol";
	invalid[2] = "openmh";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) ==
		(mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH) != NULL ? -1 : 1));
	invalid[2] = "legacy";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) ==
		(mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) != NULL ? -1 : 1));
	invalid[2] = "fixed";
	CHECK(mh_application_parse_arguments(&app, 3, invalid) == 1);

	return 0;
}

static int
check_user_data_options(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--user-data", "profiles/Player One"};

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 3, args) == -1);
	CHECK(strcmp(app.user_data_directory, args[2]) == 0);
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	CHECK(app.user_data_directory == NULL);
	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
	args[2] = "";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	args[2] = "--unlock-all";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);

	return 0;
}

static int
check_audio_options(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--audio", "0"};
	char *invalid[] = {"", "2", "-1", "01", "on"};
	unsigned int i;

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	CHECK(app.audio_mode == MH_MENUS_AUDIO_AUTO);
	CHECK(mh_application_parse_arguments(&app, 3, args) == -1);
	CHECK(app.audio_mode == MH_MENUS_AUDIO_DISABLED);
	args[2] = "1";
	CHECK(mh_application_parse_arguments(&app, 3, args) == -1);
	CHECK(app.audio_mode == MH_MENUS_AUDIO_AUTO);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		args[2] = invalid[i];
		CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	}

	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
	args[1] = "--no-audio";
	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
	args[1] = "--debug-require-audio";
#ifdef MH_DEBUG
	CHECK(mh_application_parse_arguments(&app, 2, args) == -1);
	CHECK(app.audio_mode == MH_MENUS_AUDIO_REQUIRED);
#else
	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
#endif

	return 0;
}

static int
check_simulation_rates(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--sim-rate", NULL};

	const struct simulation_rate_case valid[] = {{"1", 1}, {"60", 60},
		{"120", 120}, {"240", 240}, {"100", 100}, {"144", 144},
		{"1000", 1000}, {"4294967295", 0xffffffffUL}};

	char *invalid[] = {"0", "-1", "144.5", "100hz", "", "4294967296",
		"99999999999999999999"};

	unsigned int i;

	memset(&app, 0, sizeof(app));

	for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		args[2] = valid[i].text;

		if (mh_application_parse_arguments(&app, 3, args) != -1 ||
			app.session_options.simulation_rate != valid[i].rate) {
			fprintf(stderr,
				"--sim-rate %s: expected %lu Hz, got %u Hz\n",
				valid[i].text, valid[i].rate,
				app.session_options.simulation_rate);
			return 1;
		}
	}

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		args[2] = invalid[i];

		if (mh_application_parse_arguments(&app, 3, args) != 1) {
			fprintf(stderr, "--sim-rate '%s': expected rejection\n",
				invalid[i]);
			return 1;
		}
	}

	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);

	return 0;
}

static int
check_renderers(void)
{
	static const char *const names[] = {
		"software", "glide", "direct3d", "powervr", "openmh"};

	static const unsigned int choices[] = {MH_RENDER_CHOICE_SOFTWARE,
		MH_RENDER_CHOICE_GLIDE, MH_RENDER_CHOICE_DIRECT3D,
		MH_RENDER_CHOICE_POWERVR, MH_RENDER_CHOICE_OPENMH};

	struct mh_application app;
	char *args[] = {"openmotor", "--renderer", NULL};
	unsigned int i;

	memset(&app, 0, sizeof(app));

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		args[2] = (char *)names[i];
		CHECK(mh_application_parse_arguments(&app, 3, args) == -1);
		CHECK(app.renderer == choices[i]);
	}

	args[2] = "d3d";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);

	return 0;
}

static int
check_display_toggles(void)
{
	static const char *const options[] = {
		"--fullscreen", "--grab-mouse", "--show-fps"};

	static const char *const invalid[] = {"2", "-1", "on", "", "01"};
	static const char *const removed[] = {
		"--windowed", "--no-grab-mouse", "--hide-fps"};

	static const unsigned int masks[] = {MH_APP_OVERRIDE_FULLSCREEN,
		MH_APP_OVERRIDE_GRAB_MOUSE, MH_APP_OVERRIDE_SHOW_FPS};

	struct mh_application app;
	char *args[] = {"openmotor", NULL, NULL};
	unsigned int i, j;

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	CHECK(app.overrides == 0);

	for (i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		args[1] = (char *)options[i];
		CHECK(mh_application_parse_arguments(&app, 2, args) == 1);

		for (j = 0; j < 2; j++) {
			args[2] = j == 0 ? "0" : "1";
			CHECK(mh_application_parse_arguments(&app, 3, args) ==
				-1);
			CHECK(app.overrides == masks[i]);

			if (i == 0)
				CHECK(app.fullscreen == j);
			else if (i == 1)
				CHECK(app.grab_mouse == j);
			else
				CHECK(app.show_fps == j);
		}

		for (j = 0; j < sizeof(invalid) / sizeof(invalid[0]); j++) {
			args[2] = (char *)invalid[j];
			CHECK(mh_application_parse_arguments(&app, 3, args) ==
				1);
		}
	}

	for (i = 0; i < sizeof(removed) / sizeof(removed[0]); i++) {
		args[1] = (char *)removed[i];
		CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
	}

	return 0;
}

static int
check_debug_switches(void)
{
	struct mh_application app;
	char *settings[] = {"openmotor", "--debug-no-settings"};
	char *records[] = {"openmotor", "--debug-no-records"};
	char *intro[] = {"openmotor", "--debug-no-intro"};

	memset(&app, 0, sizeof(app));
#ifdef MH_DEBUG
	CHECK(mh_application_parse_arguments(&app, 2, settings) == -1);
	CHECK(app.no_settings);
	CHECK(mh_application_parse_arguments(&app, 2, records) == -1);
	CHECK(app.session_options.no_records);
	CHECK(mh_application_parse_arguments(&app, 2, intro) == -1);
	CHECK(app.no_intro);
	CHECK(mh_application_parse_arguments(&app, 1, intro) == -1);
	CHECK(!app.no_intro);
#else
	CHECK(mh_application_parse_arguments(&app, 2, settings) == 1);
	CHECK(mh_application_parse_arguments(&app, 2, records) == 1);
	CHECK(mh_application_parse_arguments(&app, 2, intro) == 1);
#endif
	settings[1] = "--no-settings";
	records[1] = "--no-records";
	intro[1] = "--no-intro";
	CHECK(mh_application_parse_arguments(&app, 2, intro) == 1);
	CHECK(mh_application_parse_arguments(&app, 2, settings) == 1);
	CHECK(mh_application_parse_arguments(&app, 2, records) == 1);

	return 0;
}

#ifdef MH_DEBUG
static int
check_debug_overrides(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--debug-no-settings", "--debug-name",
		"Driver", "--debug-no-records", "--debug-laps", "3",
		"--debug-record", "1", "--fullscreen", "1", "--debug-controls",
		"controls", "--debug-addresses", "addresses"};

	char *settings_conflict[] = {"openmotor", "--debug-no-settings",
		"--debug-settings", "test.cfg"};

	char *records_conflict[] = {"openmotor", "--debug-records", "records",
		"--debug-no-records"};

	char *invalid_laps[] = {"openmotor", "--debug-laps", "0"};

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(
		&app, (int)(sizeof(args) / sizeof(args[0])), args) == -1);
	CHECK(strcmp(app.player_name, "Driver") == 0);
	CHECK(app.laps == 3 && app.record_race == 1 && app.fullscreen);
	CHECK(app.no_settings && app.session_options.no_records);
	CHECK(strcmp(app.controls_directory, "controls") == 0);
	CHECK(strcmp(app.addresses_directory, "addresses") == 0);
	CHECK((app.overrides &
		(MH_APP_OVERRIDE_LAPS | MH_APP_OVERRIDE_RECORD)) ==
		(MH_APP_OVERRIDE_LAPS | MH_APP_OVERRIDE_RECORD));
	CHECK(mh_application_parse_arguments(&app, 4, settings_conflict) == 2);
	CHECK(mh_application_parse_arguments(&app, 4, records_conflict) == 2);
	CHECK(mh_application_parse_arguments(&app, 3, invalid_laps) == 1);

	return 0;
}
#endif

static int
check_diagnostic_controls(void)
{
	static const char *const toggles[] = {"--debug-window-titles",
		"--debug-menu-clock", "--debug-menu-frames",
		"--debug-skip-loading-wait", "--debug-profile-trace",
		"--debug-address-trace", "--debug-lobby-trace",
		"--debug-lobby-frames", "--debug-network-events",
		"--debug-camera-trace", "--debug-hidden-trace",
		"--debug-fixed-chase", "--debug-glenz", "--debug-music-trace",
		"--debug-audio-trace", "--debug-gpu"};

	static const char *const invalid[] = {"", "2", "-1", "01", "on"};
	struct mh_application app;
	char *args[] = {"openmotor", NULL, NULL};
	char *combined[] = {"openmotor", "--debug-window-titles", "1",
		"--debug-camera-trace", "1", "--debug-gpu", "1",
		"--debug-texture-bytes", "0", "--debug-screenshots", "pictures",
		"--debug-lobby-capture", "lobby.bmp", "--debug-camera-trace",
		"0"};

	unsigned int i, j;
	int expected;

#ifdef MH_DEBUG
	expected = -1;
#else
	expected = 1;
#endif
	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);

	/*
	 * CTest supplies the removed environment switches; defaults stay off.
	 */
	CHECK(!app.session_options.debug.window_titles &&
		!app.session_options.debug.camera_trace &&
		!app.session_options.debug.menu_clock &&
		!app.session_options.debug.texture_budget_set &&
		app.session_options.debug.screenshot_directory == NULL);

	for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++) {
		args[1] = (char *)toggles[i];
		args[2] = "1";
		CHECK(mh_application_parse_arguments(&app, 3, args) ==
			expected);
		args[2] = "0";
		CHECK(mh_application_parse_arguments(&app, 3, args) ==
			expected);
		CHECK(mh_application_parse_arguments(&app, 2, args) == 1);

		for (j = 0; j < sizeof(invalid) / sizeof(invalid[0]); j++) {
			args[2] = (char *)invalid[j];
			CHECK(mh_application_parse_arguments(&app, 3, args) ==
				1);
		}
	}

	CHECK(mh_application_parse_arguments(&app,
		(int)(sizeof(combined) / sizeof(combined[0])),
		combined) == expected);
#ifdef MH_DEBUG
	CHECK(app.session_options.debug.window_titles &&
		!app.session_options.debug.camera_trace &&
		app.session_options.debug.gpu);
	CHECK(app.session_options.debug.texture_budget_set &&
		app.session_options.debug.texture_budget == 0);
	CHECK(strcmp(app.session_options.debug.screenshot_directory,
		"pictures") == 0);
	CHECK(strcmp(app.session_options.debug.lobby_capture, "lobby.bmp") ==
		0);
#endif
	args[1] = "--debug-texture-bytes";
	args[2] = "33554432";
	CHECK(mh_application_parse_arguments(&app, 3, args) == expected);
	args[2] = "33554433";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	args[1] = "--debug-unknown";
	args[2] = "1";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);

	return 0;
}

static int
check_tool_diagnostics(void)
{
	struct mh_debug_options debug;
	char *args[] = {"tool", "track", "--debug-camera-trace", "1",
		"--renderer", "software", "--debug-texture-bytes", "0",
		"--debug-gpu", "1", "output.bmp", NULL};

	char *bad[] = {"tool", "--debug-camera-trace", "yes", NULL};
	int count;

	count = 11;
	CHECK(mh_debug_parse_arguments(&debug, &count, args, NULL) ==
		MH_ERROR_NONE);
	CHECK(count == 5 && args[5] == NULL);
	CHECK(strcmp(args[1], "track") == 0);
	CHECK(strcmp(args[2], "--renderer") == 0);
	CHECK(strcmp(args[3], "software") == 0);
	CHECK(strcmp(args[4], "output.bmp") == 0);
	CHECK(debug.camera_trace && debug.gpu);
	CHECK(mh_debug_texture_budget(&debug) != NULL &&
		*mh_debug_texture_budget(&debug) == 0);
	count = 1;
	CHECK(mh_debug_parse_arguments(&debug, &count, args, NULL) ==
		MH_ERROR_NONE);
	CHECK(!debug.camera_trace && !debug.gpu);
	CHECK(mh_debug_texture_budget(&debug) == NULL);
	count = 3;
	CHECK(mh_debug_parse_arguments(&debug, &count, bad, NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_application app;
	int result;

	if (argc > 1) {
		memset(&app, 0, sizeof(app));
		result = mh_application_parse_arguments(&app, argc, argv);
		return result < 0 ? 0 : result;
	}

	CHECK(check_debug_options() == 0);
	CHECK(check_tool_diagnostics() == 0);
	CHECK(check_diagnostic_controls() == 0);
	CHECK(check_player_options() == 0);
	CHECK(check_connect_option() == 0);
	CHECK(check_shared_syntax() == 0);
	CHECK(check_audio_options() == 0);
	CHECK(check_user_data_options() == 0);
	CHECK(check_simulation_rates() == 0);
	CHECK(check_renderers() == 0);
	CHECK(check_display_toggles() == 0);
	CHECK(check_debug_switches() == 0);
#ifdef MH_DEBUG
	CHECK(check_debug_overrides() == 0);
#endif
	puts("Client argument availability and player settings passed");

	return 0;
}

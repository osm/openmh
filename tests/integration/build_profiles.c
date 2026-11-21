#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_provider.h"
#include "client/application/application_internal.h"
#include "client/hosts/services.h"
#include "client/settings/availability.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_effects.h"
#include "engine/audio/race_profile.h"
#include "engine/input/device_choices.h"
#include "engine/settings/config.h"
#include "engine/ui/menu_graphics.h"
#include "engine/ui/menu_sound.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/service.h"

static int failures;
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#condition);                                   \
			failures++;                                            \
		}                                                              \
	} while (0)

static unsigned int
next_choice(unsigned int current, unsigned long mask, int direction)
{
	do {
		current = direction > 0 ? (current + 1) % 5 : (current + 4) % 5;
	} while (!(mask & (1UL << current)));

	return current;
}

static void
check_default_settings(unsigned int expected)
{
	struct mh_application app;
	char *args[] = {"openmotor"};

	memset(&app, 0, sizeof(app));
	CHECK(mh_application_parse_arguments(&app, 1, args) == -1);
	app.no_settings = 1;
	CHECK(mh_application_load_settings(&app, NULL) == MH_ERROR_NONE);
	CHECK(app.defaults.renderer == expected);
	CHECK(app.preferences == &app.defaults);
	CHECK(app.preferences->av.width == (expected == 0 ? 320 : 1280));
	CHECK(app.preferences->av.height == (expected == 0 ? 200 : 1024));
}

static void
check_sound_menu(unsigned long mask)
{
	struct mh_menu_sound_state state;
	struct mh_menu_sound_values values;
	struct mh_menu_sound_catalog catalog;
	struct mh_menu_sound_input input;
	struct mh_menu_sound_output output;
	unsigned long track;
	unsigned int selection, expected, i;

	memset(&state, 0, sizeof(state));
	memset(&values, 0, sizeof(values));
	memset(&input, 0, sizeof(input));
	track = 2;
	catalog.tracks = &track;
	catalog.count = 1;
	catalog.last_track = 2;
	state.selected = MH_MENU_SOUND_MODE;
	input.right = 1;
	selection = next_choice(4, mask, 1);

	for (i = 0; i < 10; i++) {
		expected = next_choice(selection, mask, 1);
		CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog,
			&input, &selection, &output,
			NULL) == MH_ERROR_NONE);
		CHECK(selection == expected);
	}
}

int
main(int argc, char **argv)
{
	struct mh_app_services services;
	const struct mh_render_service *rendering;
	const struct mh_render_backend *backends[5];
	struct mh_menu_graphics_device devices[5];
	struct mh_menu_graphics_mode mode;
	struct mh_menu_graphics_catalog catalog;
	struct mh_menu_graphics_state state;
	struct mh_menu_graphics_input input;
	struct mh_menu_output output;
	struct mh_av_settings settings;
	struct mh_settings saved;
	unsigned long render, audio, devices_mask, screenshots;
	unsigned int i, first, expected;
	int direction;

	if (argc != 5)
		return 2;

	render = strtoul(argv[1], NULL, 0);
	audio = strtoul(argv[2], NULL, 0);
	devices_mask = strtoul(argv[3], NULL, 0);
	screenshots = strtoul(argv[4], NULL, 0);
	CHECK(render != 0 && audio != 0);
	check_sound_menu(audio);
	rendering = mh_sdl_render_service();
	backends[0] = rendering->renderers->software;
	backends[1] = rendering->renderers->glide;
	backends[2] = rendering->renderers->direct3d;
	backends[3] = rendering->renderers->powervr;
	backends[4] = rendering->renderers->openmh;
	CHECK(rendering->renderers->ui != NULL);
	CHECK(mh_host_common_services(&services, ".", NULL, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 5; i++) {
		expected = (render & (1UL << i)) != 0;
		CHECK((backends[i] != NULL) == (int)expected);
		CHECK(mh_render_choice_available(i) == (int)expected);
		expected = (audio & (1UL << i)) != 0;
		CHECK(mh_race_audio_available(i) == (int)expected);
		CHECK((mh_race_profile_ops((int)i) != NULL) == (int)expected);

		for (direction = -1; direction <= 1; direction += 2)
			CHECK(mh_race_audio_next(i, direction) ==
				next_choice(i, audio, direction));
	}

	CHECK(mh_race_profile_ops(-1) == NULL);
	CHECK(mh_race_profile_ops(5) == NULL);
	CHECK(mh_race_audio_default() == next_choice(4, audio, 1));

	/* Prefer OpenMH, then Glide, then the first included profile. */
	expected = (render & 16) ? 4
		: (render & 2)	 ? 1
				: next_choice(4, render, 1);
	CHECK(mh_render_choice_default() == expected);
	check_default_settings(expected);
	CHECK(strcmp(mh_render_choice_names()[4], "OpenMH") == 0);
	CHECK(mh_render_choice_parse("openmh", &i) && i == 4);
	CHECK(mh_input_scan_available(MH_KEY_SPACE) ==
		((devices_mask & 1) != 0));
	CHECK(mh_input_scan_available(MH_KEY_MOUSE_IN) ==
		((devices_mask & 2) != 0));
	CHECK(mh_input_scan_available(MH_KEY_JOYSTICK_LEFT) ==
		((devices_mask & 4) != 0));
	CHECK((mh_sdl_input_provider()->ops->force != NULL) ==
		((devices_mask & 8) != 0));

	for (i = 0; i < MH_SCREENSHOT_FORMAT_COUNT; i++) {
		CHECK(mh_screenshot_format_available(i) ==
			((screenshots & (1UL << i)) != 0));
		CHECK((services.screenshot_encoders[i] != NULL) ==
			((screenshots & (1UL << i)) != 0));
	}

	memset(&saved, 0, sizeof(saved));

	for (i = 0; i < 5; i++) {
		saved.renderer = i;
		saved.av.race_audio = i;
		saved.av.screenshot_format = i % MH_SCREENSHOT_FORMAT_COUNT;
		mh_settings_apply_availability(&saved);
		CHECK(saved.renderer ==
			((render & (1UL << i)) ? i
					: mh_render_choice_default()));
		CHECK(saved.av.race_audio ==
			((audio & (1UL << i)) ? i : next_choice(4, audio, 1)));

		if (screenshots)
			CHECK(mh_screenshot_format_available(
				saved.av.screenshot_format));
	}

	/* Menu IDs stay stable while navigation skips unavailable slots. */
	memset(devices, 0, sizeof(devices));
	memset(&catalog, 0, sizeof(catalog));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	mode.width = 640;
	mode.height = 480;

	for (i = 0; i < 5; i++) {
		devices[i].name = mh_render_choice_names()[i];
		devices[i].modes = &mode;
		devices[i].count = (render & (1UL << i)) != 0;
	}

	catalog.devices = devices;
	catalog.count = 5;
	first = next_choice(4, render, 1);
	state.device = first;
	CHECK(mh_av_settings_init(&settings, 255, 255, NULL) == MH_ERROR_NONE);
	input.right = 1;

	for (i = 0; i < 10; i++) {
		expected = next_choice(state.device, render, 1);

		if (expected < state.device)
			expected = state.device;

		CHECK(mh_menu_graphics_update(&state, &settings, &catalog,
			&input, &output, NULL) == MH_ERROR_NONE);
		CHECK(state.device == expected);
	}

	return failures != 0;
}

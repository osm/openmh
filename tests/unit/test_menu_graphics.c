#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_graphics.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Graphics line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
invalid_requests(void)
{
	struct mh_menu_graphics_state state, saved_state;
	struct mh_menu_graphics_input input;
	struct mh_menu_output output, saved_output;
	struct mh_menu_graphics_catalog catalog;
	struct mh_menu_graphics_device devices[2];
	struct mh_menu_graphics_mode modes[2];
	struct mh_av_settings settings, saved_settings;
	unsigned int i;

	for (i = 0; i < 20; i++) {
		memset(&state, 0, sizeof(state));
		memset(&input, 0, sizeof(input));
		memset(&output, 0xa5, sizeof(output));
		CHECK(mh_av_settings_init(&settings, 255, 255, NULL) ==
			MH_ERROR_NONE);
		modes[0].width = modes[1].width = 640;
		modes[0].height = modes[1].height = 400;
		memset(devices, 0, sizeof(devices));
		devices[0].modes = modes;
		devices[1].modes = modes + 1;
		devices[0].count = devices[1].count = 1;
		devices[0].motion_blur = devices[1].motion_blur = 1;
		catalog.devices = devices;
		catalog.fullscreen = 0;
		catalog.presentation = 0;
		catalog.screenshots = 1;
		catalog.count = 2;
		input.escape = input.down = input.right = input.left =
			input.held_right = input.held_left = 1;

		if (i == 0)
			state.selected = 23;

		if (i == 1)
			state.device = 2;

		if (i == 2)
			state.mode = 1;

		if (i == 3)
			catalog.count = 0;

		if (i == 4)
			catalog.count = 65;

		if (i == 5)
			catalog.devices = NULL;

		if (i == 6)
			devices[1].modes = NULL;

		if (i == 7) {
			devices[1].count = 0;
			state.device = 1;
		}

		if (i == 8)
			devices[1].count = 4097;

		if (i == 9)
			settings.info_detail = 3;

		if (i == 10)
			settings.checkpoint_delay = 10001;

		if (i == 11)
			settings.view_distance = 151;

		if (i == 12) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			input.elapsed_ms = ULONG_MAX;
		}

		if (i == 13) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			modes[1].height = ULONG_MAX;
		}

		if (i == 14)
			settings.detail_mode = 5;

		saved_state = state;
		saved_settings = settings;
		saved_output = output;
		CHECK(mh_menu_graphics_update(i == 15 ? NULL : &state,
			i == 16 ? NULL : &settings,
			i == 17 ? NULL : &catalog,
			i == 18 ? NULL : &input, i == 19 ? NULL : &output,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
			memcmp(&settings, &saved_settings, sizeof(settings)) ==
				0 &&
			memcmp(&output, &saved_output, sizeof(output)) == 0);
	}

	return 0;
}

int
main(void)
{
	struct mh_menu_graphics_state state;
	struct mh_menu_graphics_input input;
	struct mh_menu_output output;
	struct mh_menu_graphics_catalog catalog;
	struct mh_menu_graphics_device device;
	struct mh_menu_graphics_mode modes[3];
	struct mh_av_settings settings;

	CHECK(invalid_requests() == 0);
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&device, 0, sizeof(device));
	modes[0].width = modes[1].width = 640;
	modes[0].height = 480;
	modes[1].height = 400;
	modes[2].width = 800;
	modes[2].height = 600;
	device.modes = modes;
	device.count = 3;
	device.motion_blur = 1;
	catalog.devices = &device;
	catalog.fullscreen = 0;
	catalog.presentation = 0;
	catalog.screenshots = 1;
	catalog.count = 1;
	CHECK(mh_av_settings_init(&settings, 255, 255, NULL) == MH_ERROR_NONE);

	/*
	 * Fullscreen belongs to the window system, across all renderer presets.
	 */
	state.selected = MH_MENU_GRAPHICS_FULLSCREEN;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);
	catalog.fullscreen = 1;
	state.selected = MH_MENU_GRAPHICS_MODE;
	input.down = 1;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_FULLSCREEN &&
		settings.fullscreen);
	input.right = 0;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);

	/* FPS display works without native presentation controls or Custom. */
	CHECK(state.selected == MH_MENU_GRAPHICS_SHOW_FPS &&
		!settings.show_fps);
	input.down = 0;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.show_fps == 1);
	CHECK(mh_av_settings_preset(&settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.show_fps == 1);
	settings.show_fps = 2;
	CHECK(mh_menu_graphics_validate(&state, &settings, &catalog, NULL) ==
		MH_ERROR_INVALID);
	settings.show_fps = 1;
	input.right = 0;
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_SCREENSHOT_FORMAT);
	input.down = 0;
	input.right = 1;
	CHECK(settings.screenshot_format == MH_SCREENSHOT_PNG);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.screenshot_format == MH_SCREENSHOT_TGA);
	CHECK(mh_av_settings_preset(&settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.screenshot_format == MH_SCREENSHOT_TGA);
	input.right = 0;
	input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.screenshot_format == MH_SCREENSHOT_PNG);

	input.down = 0;
	input.up = input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_SHOW_FPS &&
		!settings.show_fps);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_FULLSCREEN &&
		!settings.fullscreen);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_MODE);
	catalog.fullscreen = 0;
	catalog.presentation = 0;
	catalog.screenshots = 1;
	memset(&input, 0, sizeof(input));
	state.selected = MH_MENU_GRAPHICS_DEVICE;

	/*
	 * Native presentation rows are visible even with a non-Custom preset.
	 */
	catalog.presentation = 1;
	catalog.screenshots = 1;
	state.selected = MH_MENU_GRAPHICS_MODE;
	input.down = input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_VSYNC && settings.vsync == 1);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_MAX_FPS &&
		settings.max_fps == 0);
	input.down = 0;
	input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.max_fps == 0 && !state.fps_editing);
	input.left = 0;
	input.down = input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_SHOW_FPS &&
		settings.show_fps == 1 && settings.max_fps == 0 &&
		settings.vsync == 1);

	/* Browser catalogs omit the screenshot row in both directions. */
	catalog.presentation = 0;
	catalog.screenshots = 0;
	memset(&input, 0, sizeof(input));
	state.selected = MH_MENU_GRAPHICS_SHOW_FPS;
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_BRIGHTNESS);
	input.down = 0;
	input.up = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_SHOW_FPS);
	state.selected = MH_MENU_GRAPHICS_SCREENSHOT_FORMAT;
	CHECK(mh_menu_graphics_validate(&state, &settings, &catalog, NULL) ==
		MH_ERROR_UNSUPPORTED);
	catalog.screenshots = 1;
	memset(&input, 0, sizeof(input));
	state.selected = MH_MENU_GRAPHICS_DEVICE;

	/*
	 * Boundary device arrows still remap: equal width ties select the
	 * first mode, even when the old mode matched both dimensions exactly.
	 */
	state.mode = 1;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.mode == 0 && output.count == 1 &&
		output.events[0].value == 15);
	state.selected = 7;
	settings.detail_mode = 3;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.detail_mode == 4 && settings.view_distance == 80 &&
		settings.smoke == 1);
	state.selected = 20;
	input.right = 0;
	input.left = 1;
	settings.perspective = 4;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.perspective == 8);
	input.right = 1;
	input.left = 0;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.perspective == 16);
	state.selected = 2;
	input.escape = input.up = input.down = input.held_right =
		input.held_left = 1;
	input.elapsed_ms = 16;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 6 && output.events[0].value == 1 &&
		output.events[1].kind == MH_MENU_PAGE &&
		output.events[1].value == MH_MENU_OPTIONS &&
		output.events[4].value == 16 && output.events[5].value == 17);
	memset(&input, 0, sizeof(input));
	state.selected = MH_MENU_GRAPHICS_CAR_SHADING;
	settings.car_shading = MH_AV_SHADING_REFLECTION;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.car_shading == MH_AV_SHADING_REFLECTION);
	device.glenz = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.car_shading == MH_AV_SHADING_GLENZ);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.car_shading == MH_AV_SHADING_GLENZ);

	/*
	 * Selecting an incompatible device discards the unsupported material.
	 */
	device.glenz = 0;
	state.selected = MH_MENU_GRAPHICS_DEVICE;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.car_shading == MH_AV_SHADING_REFLECTION);
	memset(&input, 0, sizeof(input));
	state.selected = MH_MENU_GRAPHICS_MOTION_BLUR;
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_MOTION_BLUR);
	device.texture_precision = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_TEXTURE_PRECISION);
	input.down = 0;
	input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_16BIT);
	input.left = 0;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_32BIT);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_32BIT &&
		output.events[0].value == MH_MENU_SOUND_VALUE_LIMIT);
	device.texture_precision = 0;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_32BIT);
	device.texture_filter = 1;
	state.selected = MH_MENU_GRAPHICS_MOTION_BLUR;
	memset(&input, 0, sizeof(input));
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_TEXTURE_FILTER);
	input.down = 0;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_filter == MH_AV_FILTER_NEAREST);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_filter == MH_AV_FILTER_BILINEAR);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_filter == MH_AV_FILTER_TRILINEAR);
	device.texture_filter = 0;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);
	device.frame_queue = 1;
	state.selected = MH_MENU_GRAPHICS_MOTION_BLUR;
	memset(&input, 0, sizeof(input));
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_GRAPHICS_FRAME_QUEUE);
	input.down = 0;
	input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.frame_queue == MH_AV_FRAME_QUEUE_ONE);
	input.left = 0;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.frame_queue == MH_AV_FRAME_QUEUE_TWO);
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.frame_queue == MH_AV_FRAME_QUEUE_THREE);
	input.right = 0;
	input.up = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		state.selected == MH_MENU_GRAPHICS_MOTION_BLUR);
	device.frame_queue = 0;
	state.selected = MH_MENU_GRAPHICS_FRAME_QUEUE;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);

	device.blur_history = 1;
	state.selected = MH_MENU_GRAPHICS_MOTION_BLUR;
	memset(&input, 0, sizeof(input));
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		state.selected == MH_MENU_GRAPHICS_TRIPLE_BUFFER);
	input.down = 0;
	input.right = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.triple_buffer == 1);
	input.right = 0;
	input.left = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		settings.triple_buffer == 0);
	input.left = 0;
	input.up = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		state.selected == MH_MENU_GRAPHICS_MOTION_BLUR);
	device.blur_history = 0;
	state.selected = MH_MENU_GRAPHICS_TRIPLE_BUFFER;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);

	/*
	 * PowerVR hides unsupported rows without rewriting their saved values.
	 */
	device.motion_blur = 0;
	device.frame_queue = 1;
	settings.motion_blur = 1;
	settings.texture_filter = MH_AV_FILTER_TRILINEAR;
	settings.triple_buffer = 1;
	state.selected = MH_MENU_GRAPHICS_VIEW_DISTANCE;
	memset(&input, 0, sizeof(input));
	input.down = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		state.selected == MH_MENU_GRAPHICS_FRAME_QUEUE);
	input.down = 0;
	input.up = 1;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE &&
		state.selected == MH_MENU_GRAPHICS_VIEW_DISTANCE);
	CHECK(settings.motion_blur == 1 &&
		settings.texture_filter == MH_AV_FILTER_TRILINEAR &&
		settings.triple_buffer == 1);
	state.selected = MH_MENU_GRAPHICS_MOTION_BLUR;
	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(!mh_menu_graphics_row_available(
		&device, MH_MENU_GRAPHICS_MOTION_BLUR));

	puts("Graphics: validation, navigation, deferred presets and material capabilities passed");

	return 0;
}

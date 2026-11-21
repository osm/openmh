#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/render_graphics_catalog.h"
#include "client/settings/render_choice.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/viewport.h"
#include "engine/window/graphics_catalog.h"

static int
check_profile(unsigned long width, unsigned long height,
	unsigned long wanted_width, unsigned long wanted_height,
	unsigned int device)
{
	struct mh_graphics_catalog *owner;
	const struct mh_menu_graphics_catalog *catalog;
	const struct mh_menu_graphics_mode *modes;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, selected, count;
	int success;

	owner = NULL;
	selected = 9999;
	status = mh_graphics_catalog_open(width, height,
		mh_render_choice_modes(), MH_RENDER_CHOICE_COUNT, device,
		&owner, &selected, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 0;
	}

	catalog = mh_graphics_catalog_choices(owner);
	count = catalog->devices[device].count;
	modes = catalog->devices[device].modes;
	success = catalog->count == MH_RENDER_CHOICE_COUNT &&
		strcmp(catalog->devices[0].name, "Software") == 0 &&
		strcmp(catalog->devices[1].name, "Glide") == 0 &&
		strcmp(catalog->devices[2].name, "Direct3D") == 0 &&
		strcmp(catalog->devices[3].name, "PowerVR") == 0 &&
		strcmp(catalog->devices[MH_RENDER_CHOICE_SOFTWARE].name,
			"Software") == 0 &&
		catalog->devices[MH_RENDER_CHOICE_SOFTWARE].modes == modes &&
		(!mh_render_choice_available(MH_RENDER_CHOICE_GLIDE) ||
			catalog->devices[MH_RENDER_CHOICE_SOFTWARE].count <
				catalog->devices[MH_RENDER_CHOICE_GLIDE]
					.count) &&
		strcmp(catalog->devices[MH_RENDER_CHOICE_GLIDE].name,
			"Glide") == 0 &&
		strcmp(catalog->devices[MH_RENDER_CHOICE_DIRECT3D].name,
			"Direct3D") == 0 &&
		catalog->devices[MH_RENDER_CHOICE_DIRECT3D].glenz &&
		catalog->devices[MH_RENDER_CHOICE_DIRECT3D].texture_filter &&
		catalog->devices[MH_RENDER_CHOICE_DIRECT3D].texture_precision &&
		catalog->devices[MH_RENDER_CHOICE_DIRECT3D].frame_queue &&
		catalog->devices[MH_RENDER_CHOICE_DIRECT3D].blur_history &&
		!catalog->devices[MH_RENDER_CHOICE_GLIDE].blur_history &&
		!catalog->devices[MH_RENDER_CHOICE_SOFTWARE].blur_history &&
		strcmp(catalog->devices[MH_RENDER_CHOICE_POWERVR].name,
			"PowerVR") == 0 &&
		!catalog->devices[MH_RENDER_CHOICE_POWERVR].glenz &&
		!catalog->devices[MH_RENDER_CHOICE_POWERVR].texture_filter &&
		!catalog->devices[MH_RENDER_CHOICE_POWERVR].texture_precision &&
		!catalog->devices[MH_RENDER_CHOICE_POWERVR].motion_blur &&
		!catalog->devices[MH_RENDER_CHOICE_POWERVR].blur_history &&
		catalog->devices[MH_RENDER_CHOICE_POWERVR].frame_queue &&
		(!mh_render_choice_available(MH_RENDER_CHOICE_GLIDE) ||
			catalog->devices[MH_RENDER_CHOICE_GLIDE].count >= 19);

	if (!mh_render_choice_available(device)) {
		mh_graphics_catalog_close(owner);
		return success && count == 0;
	}

	success = success && count <= 4096 && selected < count;

	if (success)
		success = modes[selected].width == wanted_width &&
			modes[selected].height == wanted_height;

	for (i = 0; success && i < count; i++) {
		success = modes[i].width >= MH_VIEW_MIN_WIDTH &&
			modes[i].width <=
				mh_render_choice_modes()[device].max_width &&
			modes[i].height >= MH_VIEW_MIN_HEIGHT &&
			modes[i].height <= MH_VIEW_MAX_HEIGHT;

		if (i != 0)
			success = success &&
				(modes[i].width > modes[i - 1].width ||
					(modes[i].width == modes[i - 1].width &&
						modes[i].height >
							modes[i - 1].height));
	}

	mh_graphics_catalog_close(owner);

	return success;
}

static int
check_glide(unsigned long width, unsigned long height,
	unsigned long wanted_width, unsigned long wanted_height)
{
	return check_profile(width, height, wanted_width, wanted_height,
		MH_RENDER_CHOICE_GLIDE);
}

int
main(void)
{
	struct mh_graphics_catalog *owner;
	unsigned int selected;
	int success;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}

	owner = NULL;
	selected = 9999;
	success = mh_graphics_catalog_open(640, 480, mh_render_choice_modes(),
			MH_RENDER_CHOICE_COUNT, MH_RENDER_CHOICE_GLIDE, NULL,
			&selected, NULL) != MH_ERROR_NONE &&
		selected == 9999 &&
		mh_graphics_catalog_open(640, 480, mh_render_choice_modes(),
			MH_RENDER_CHOICE_COUNT, MH_RENDER_CHOICE_GLIDE, &owner,
			NULL, NULL) != MH_ERROR_NONE &&
		owner == NULL;
	success = success && check_glide(854, 481, 854, 481) &&
		check_glide(320, 200, 320, 200) &&
		check_glide(4096, 2160, 4096, 2160) &&
		check_glide(1280, 720, 1280, 720) &&
		check_glide(319, 200, 960, 640) &&
		check_glide(640, 199, 960, 640) &&
		check_glide(4097, 200, 960, 640) &&
		check_glide(640, 2161, 960, 640) &&
		check_glide(MH_U32_MAX, MH_U32_MAX, 960, 640);
	success = success &&
		check_profile(854, 481, 854, 481, MH_RENDER_CHOICE_SOFTWARE) &&
		check_profile(
			1023, 768, 1023, 768, MH_RENDER_CHOICE_SOFTWARE) &&
		check_profile(1024, 768, 960, 640, MH_RENDER_CHOICE_SOFTWARE) &&
		check_profile(1920, 1080, 960, 640, MH_RENDER_CHOICE_SOFTWARE);
	success = success &&
		check_profile(854, 481, 854, 481, MH_RENDER_CHOICE_DIRECT3D) &&
		check_profile(
			1920, 1080, 1920, 1080, MH_RENDER_CHOICE_DIRECT3D) &&
		check_profile(1920, 1080, 1920, 1080, MH_RENDER_CHOICE_POWERVR);
	mh_graphics_catalog_close(NULL);
	SDL_Quit();

	if (success)
		puts("Native graphics catalog: exact saved sizes, fallback, unique sorted bounded modes passed");

	return success ? 0 : 1;
}

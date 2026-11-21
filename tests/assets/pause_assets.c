#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/asset_ui.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/ui/pause.h"

int
main(int argc, char *argv[])
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	static const float background[4] = {0.15f, 0.2f, 0.25f, 1};
	struct mh_pause_state state;
	struct mh_pause_settings settings;
	struct mh_pause_input input;
	struct mh_pause_output output;
	struct mh_renderer *renderer;
	struct mh_error error;
	enum mh_error_code status;
	char path[1024], suffix[32];
	unsigned int scenario;
	int success;

	if (argc != 3 || strlen(argv[2]) > 900)
		return 2;

	status = mh_hud_assets_load(argv[1], &assets, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Pause assets: %s\n", error.message);
		return 1;
	}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		free(assets.atlas.image.rgba);
		return 1;
	}

	renderer = mh_sdl_renderer_create_scene(NULL, NULL, 0);
	success = renderer != NULL &&
		mh_renderer_ui_upload(renderer, &assets.atlas) &&
		assets.hardware_height == 12;

	for (scenario = 0; scenario < 7 && success; scenario++) {
		mh_pause_init(&state, NULL);
		state.visible = scenario != 6;
		state.page = scenario == 4 ? 1 : scenario == 5 ? 2 : 0;
		state.selected[0] = scenario == 1 ? 1 : 0;
		state.selected[1] = 1;
		memset(&settings, 0, sizeof(settings));
		memset(&input, 0, sizeof(input));
		settings.shading = 2;
		settings.perspective = 16;
		settings.distance = 100;
		settings.effects_volume = 254;
		settings.cd_volume = 127;
		input.restart_ok = 1;

		if (scenario == 1)
			input.mode = 1;
		else if (scenario == 2 || scenario == 3)
			input.mode = 4;
		else
			input.mode = 0;
		status = mh_pause_update(
			&state, &settings, &input, &output, &error);
		mh_ui_begin(&list, MH_VIEW_WIDTH, MH_VIEW_HEIGHT);
		success = status == MH_ERROR_NONE &&
			mh_ui_quad_scaled(&list, &assets.atlas, &assets.solid,
				0, 0, MH_VIEW_WIDTH, MH_VIEW_HEIGHT,
				background) &&
			mh_pause_draw(&assets, &state, &output.display,
				input.mode, scenario == 2, &list) &&
			(scenario == 6 ? list.count == 6 : list.count > 6);
		sprintf(suffix, "-%u.bmp", scenario);
		strcpy(path, argv[2]);
		strcat(path, suffix);

		if (success)
			success = mh_renderer_ui_frame(renderer, &list) &&
				mh_renderer_capture(renderer, path);
	}

	if (!success)
		fprintf(stderr, "Pause asset/render check failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();
	free(assets.atlas.image.rgba);

	if (success)
		puts("Seven pause layouts rendered with the original hardware font, including hidden state");

	return !success;
}

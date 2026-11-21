#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "../support/tron_render.h"
#include "backends/render/sdl_gpu_provider.h"

int
main(int argc, char *argv[])
{
	int status;

	if (argc != 2 || strlen(argv[1]) > 900) {
		fprintf(stderr, "Usage: mh-tron-render CAPTURE_PREFIX\n");
		return 2;
	}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}

	status =
		mh_tron_render_check(mh_sdl_gpu_glide_backend(), NULL, argv[1]);

	if (status != 0)
		fprintf(stderr, "TRON GPU qualification failed: %s\n",
			SDL_GetError());

	SDL_Quit();

	return status;
}

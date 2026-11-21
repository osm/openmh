/*
 * Compile-only check of project C89 code calling SDL3's public API.
 * SDL headers are external system headers and can use compiler extensions.
 */
#include <SDL3/SDL.h>

int	mh_sdl89_probe(void);

int
mh_sdl89_probe(void)
{
	SDL_GPUDevice *device;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		return 1;
	}

	device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, 0, NULL);

	if (device == NULL) {
		SDL_Quit();
		return 1;
	}

	SDL_DestroyGPUDevice(device);
	SDL_Quit();

	return 0;
}

#include "backends/render/sdl_gpu_device.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_shaders.h"

SDL_GPUDevice *
mh_gpu_device_create(int diagnostics)
{
	return SDL_CreateGPUDevice(
		mh_gpu_shader_formats(), diagnostics != 0, "vulkan");
}

enum mh_error_code
mh_gpu_set_frame_queue(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (gpu->window == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"headless rendering has no presentation queue");

	if (!SDL_SetGPUAllowedFramesInFlight(gpu->device, frames))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_gpu_set_vsync(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;
	SDL_GPUPresentMode mode;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (gpu->window == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"headless rendering has no VSync");

	mode = enabled ? SDL_GPU_PRESENTMODE_VSYNC
		: SDL_GPU_PRESENTMODE_IMMEDIATE;

	if (!SDL_WindowSupportsGPUPresentMode(gpu->device, gpu->window, mode)) {
		/*
		 * Mailbox replaces pending images without throttling to
		 * refresh.
		 */
		mode = SDL_GPU_PRESENTMODE_MAILBOX;

		if (!SDL_WindowSupportsGPUPresentMode(
			gpu->device, gpu->window, mode))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"this display supports only synchronized presentation");
	}

	if (!SDL_SetGPUSwapchainParameters(gpu->device, gpu->window,
		SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

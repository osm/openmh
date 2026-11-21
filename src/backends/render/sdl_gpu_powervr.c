#include "backends/render/sdl_gpu_backend.h"
#include "backends/render/sdl_gpu_provider.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/window/sdl.h"

#if MH_RENDER_BUILD_POWERVR
static enum mh_error_code
open_powervr(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;

	(void)context;

	if (window != NULL && mh_sdl_window(window) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR presentation requires an SDL window");

	gpu = mh_gpu_powervr_open(mh_sdl_window(window), meshes, count, width,
		height, (flags & MH_RENDERER_DIAGNOSTICS) != 0);

	if (gpu == NULL)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	*output = &gpu->base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#endif

const struct mh_render_backend *
mh_sdl_gpu_powervr_backend(void)
{
#if MH_RENDER_BUILD_POWERVR
	static const struct mh_render_backend backend = {"PowerVR (SDL GPU)",
		MH_RENDER_POWERVR, open_powervr, &mh_sdl_gpu_ops, NULL};

	return &backend;
#else

	return NULL;
#endif
}

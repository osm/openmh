#include "backends/render/sdl_gpu_backend.h"
#include "backends/render/sdl_gpu_provider.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

#if MH_RENDER_BUILD_OPENMH
static enum mh_error_code
open_openmh(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct mh_renderer *renderer;
	struct mh_gpu_renderer *gpu;
	enum mh_error_code status;

	(void)context;
	renderer = NULL;
	status = mh_sdl_gpu_open(NULL, window, meshes, count, width, height,
		flags, &renderer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (!mh_gpu_openmh_create_pipelines(
		gpu->device, &gpu->openmh_pipelines)) {
		mh_error_set(error_out, MH_ERROR_BACKEND, SDL_GetError());
		mh_gpu_renderer_destroy(gpu);
		return MH_ERROR_BACKEND;
	}

	*output = renderer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
#endif

const struct mh_render_backend *
mh_sdl_gpu_openmh_backend(void)
{
#if MH_RENDER_BUILD_OPENMH
	static const struct mh_render_backend backend = {"OpenMH (SDL GPU)",
		MH_RENDER_OPENMH, open_openmh, &mh_sdl_gpu_ops, NULL};

	return &backend;
#else

	return NULL;
#endif
}

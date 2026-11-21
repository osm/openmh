#include "backends/render/sdl_gpu_backend.h"
#include "backends/render/sdl_gpu_provider.h"

const struct mh_render_backend *
mh_sdl_gpu_glide_backend(void)
{
	static const struct mh_render_backend backend = {"Glide (SDL GPU)",
		MH_RENDER_GLIDE, mh_sdl_gpu_open, &mh_sdl_gpu_ops, NULL};

	return &backend;
}

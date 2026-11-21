#include "backends/render/sdl_gpu_backend.h"
#include "backends/render/sdl_gpu_provider.h"

const struct mh_render_backend *
mh_sdl_gpu_direct3d_backend(void)
{
	static const struct mh_render_backend backend = {"Direct3D (SDL GPU)",
		MH_RENDER_DIRECT3D, mh_sdl_gpu_open, &mh_sdl_gpu_ops, NULL};

	return MH_RENDER_BUILD_DIRECT3D ? &backend : NULL;
}

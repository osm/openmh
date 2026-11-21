#include "backends/render/webgl_backend.h"
#include "backends/render/webgl_provider.h"

const struct mh_render_backend *
mh_webgl_direct3d_backend(void)
{
	static const struct mh_render_backend backend = {"Direct3D (WebGL 2)",
		MH_RENDER_DIRECT3D, mh_webgl_open, &mh_webgl_ops, NULL};

	return MH_RENDER_BUILD_DIRECT3D ? &backend : NULL;
}

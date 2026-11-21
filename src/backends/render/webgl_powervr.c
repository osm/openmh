#include "backends/render/webgl_backend.h"
#include "backends/render/webgl_provider.h"

const struct mh_render_backend *
mh_webgl_powervr_backend(void)
{
	static const int powervr = 1;
	static const struct mh_render_backend backend = {"PowerVR (WebGL 2)",
		MH_RENDER_POWERVR, mh_webgl_open, &mh_webgl_ops, &powervr};

	return MH_RENDER_BUILD_POWERVR ? &backend : NULL;
}

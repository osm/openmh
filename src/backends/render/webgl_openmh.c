#include "backends/render/webgl_backend.h"
#include "backends/render/webgl_provider.h"

const struct mh_render_backend *
mh_webgl_openmh_backend(void)
{
	static const struct mh_render_backend backend = {"OpenMH (WebGL 2)",
		MH_RENDER_OPENMH, mh_webgl_open, &mh_webgl_ops, NULL};

	return MH_RENDER_BUILD_OPENMH ? &backend : NULL;
}

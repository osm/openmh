#include "backends/render/webgl_backend.h"
#include "backends/render/webgl_provider.h"

const struct mh_render_backend *
mh_webgl_ui_backend(void)
{
	static const struct mh_render_backend backend = {"Glide (WebGL 2)",
		MH_RENDER_GLIDE, mh_webgl_open, &mh_webgl_ops, NULL};

	return &backend;
}

const struct mh_render_backend *
mh_webgl_glide_backend(void)
{
	return MH_RENDER_BUILD_GLIDE ? mh_webgl_ui_backend() : NULL;
}

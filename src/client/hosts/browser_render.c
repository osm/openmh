#include "backends/render/software_provider.h"
#include "backends/render/webgl_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/hosts/browser_render.h"
#include "interfaces/render/service.h"

const struct mh_render_service *
mh_browser_render_service(void)
{
	static struct mh_render_service service;
	static struct mh_render_set renderers;
#if MH_RENDER_BUILD_SOFTWARE
	static struct mh_render_backend software;
	static struct mh_software_output output;
#endif

	if (service.windows == NULL) {
		service.windows = mh_sdl_window_system();
		service.canvases = mh_webgl_canvas_provider();
#if MH_RENDER_BUILD_SOFTWARE
		output.canvases = service.canvases;
		mh_software_backend_init(&software, &output, NULL);
		renderers.software = &software;
		renderers.ui = &software;
#else
		renderers.ui = mh_webgl_ui_backend();
#endif
#if MH_RENDER_BUILD_GLIDE
		renderers.glide = mh_webgl_glide_backend();
#endif
#if MH_RENDER_BUILD_DIRECT3D
		renderers.direct3d = mh_webgl_direct3d_backend();
#endif
#if MH_RENDER_BUILD_POWERVR
		renderers.powervr = mh_webgl_powervr_backend();
#endif
#if MH_RENDER_BUILD_OPENMH
		renderers.openmh = mh_webgl_openmh_backend();
#endif
		service.renderers = &renderers;
	}

	return &service;
}

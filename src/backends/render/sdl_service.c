#include "backends/render/sdl_canvas_provider.h"
#include "backends/render/sdl_gpu_provider.h"
#include "backends/render/sdl_provider.h"
#include "backends/render/software_provider.h"
#include "backends/window/sdl_provider.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/capture.h"
#include "interfaces/render/service.h"
#include "interfaces/window/backend.h"

#if MH_RENDER_BUILD_SOFTWARE
static enum mh_error_code
save_image(void *context, const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	(void)context;

	return mh_capture_image(mh_sdl_image_capture(), image, path, error_out);
}

#endif

const struct mh_render_service *
mh_sdl_render_service(void)
{
	static struct mh_render_service service;
	static struct mh_render_set renderers;
#if MH_RENDER_BUILD_SOFTWARE
	static struct mh_render_backend software;
	static struct mh_software_output output;
#endif

	if (service.windows == NULL) {
		service.windows = mh_sdl_window_system();
		service.canvases = mh_sdl_canvas_provider();
		service.capture = mh_sdl_image_capture();
#if MH_RENDER_BUILD_SOFTWARE
		output.canvases = service.canvases;
		output.capture = save_image;
		mh_software_backend_init(&software, &output, NULL);
#endif
#if MH_RENDER_BUILD_GLIDE
		renderers.glide = mh_sdl_gpu_glide_backend();
#endif
#if MH_RENDER_BUILD_SOFTWARE
		renderers.software = &software;
#endif
#if MH_RENDER_BUILD_DIRECT3D
		renderers.direct3d = mh_sdl_gpu_direct3d_backend();
#endif
#if MH_RENDER_BUILD_POWERVR
		renderers.powervr = mh_sdl_gpu_powervr_backend();
#endif
#if MH_RENDER_BUILD_OPENMH
		renderers.openmh = mh_sdl_gpu_openmh_backend();
#endif
#if MH_RENDER_BUILD_GPU
		renderers.ui = mh_sdl_gpu_glide_backend();
#else
		renderers.ui = &software;
#endif
		service.renderers = &renderers;
	}

	return &service;
}

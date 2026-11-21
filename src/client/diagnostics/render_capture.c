#include "backends/render/sdl_canvas_provider.h"
#include "client/diagnostics/render_capture.h"
#include "interfaces/render/capture.h"

const struct mh_image_capture *
mh_diagnostic_capture(void)
{
	return mh_sdl_image_capture();
}

#include <stddef.h>

#include "backends/image/png_provider.h"
#include "backends/image/tga_provider.h"
#include "client/hosts/screenshots.h"
#include "engine/settings/av.h"

const struct mh_image_encoder *
mh_host_screenshot_encoder(unsigned int format)
{
	switch (format) {
#if MH_SCREENSHOT_BUILD_PNG
	case MH_SCREENSHOT_PNG:
		return mh_image_png_encoder();
#endif
#if MH_SCREENSHOT_BUILD_TGA
	case MH_SCREENSHOT_TGA:
		return mh_image_tga_encoder();
#endif
	default:
		return NULL;
	}
}

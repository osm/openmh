#include "engine/settings/av.h"

int
mh_screenshot_format_available(unsigned int format)
{
	return (format == MH_SCREENSHOT_PNG && MH_SCREENSHOT_BUILD_PNG) ||
		(format == MH_SCREENSHOT_TGA && MH_SCREENSHOT_BUILD_TGA);
}

unsigned int
mh_screenshot_format_default(void)
{
	return MH_SCREENSHOT_BUILD_PNG ? MH_SCREENSHOT_PNG : MH_SCREENSHOT_TGA;
}

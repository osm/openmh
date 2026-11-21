#ifndef MH_HOST_SCREENSHOTS_H
#define MH_HOST_SCREENSHOTS_H

struct mh_image_encoder;

/* NULL for an encoder excluded by this build. Uses mh_screenshot_format IDs. */
const struct mh_image_encoder	*mh_host_screenshot_encoder(
	unsigned int format);

#endif

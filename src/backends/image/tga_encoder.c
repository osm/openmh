#include "backends/image/tga_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/image/encoder_backend.h"

static enum mh_error_code
tga_encode(void *context, const struct mh_picture *picture,
	struct mh_file *output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_tga_encode(picture, output, error_out);
}

const struct mh_image_encoder *
mh_image_tga_encoder(void)
{
	static const struct mh_image_encoder encoder = {
		"tga", NULL, tga_encode};

	return &encoder;
}

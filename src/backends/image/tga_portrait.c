#include <string.h>

#include "backends/image/tga_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/image/decoder_backend.h"

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;

	if (options->palette != NULL || options->alpha != MH_IMAGE_ALPHA_FILE)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"results portrait has no source palette");

	status = mh_image_tga_decode_portrait(data, size, output, error_out);

	if (status == MH_ERROR_NONE && palette != NULL)
		memset(palette, 0, sizeof(*palette));

	return status;
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	(void)data;
	(void)size;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"results portrait has no source palette");
}

const struct mh_image_decoder_provider *
mh_image_tga_portrait_decoder(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_image_decoder_provider provider = {
		&ops, NULL, NULL, NULL};

	return &provider;
}

#include "backends/image/ilbm_provider.h"
#include "engine/assets/ilbm.h"
#include "interfaces/image/decoder_backend.h"

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette_output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_ilbm_decode_rgba(data, size, options->palette,
		options->alpha == MH_IMAGE_ALPHA_INDEX_ZERO, output,
		palette_output, error_out);
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_ilbm_decode_indexed(data, size, output, error_out);
}

const struct mh_image_decoder_provider *
mh_image_ilbm_decoder(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_image_decoder_provider provider = {
		&ops, NULL, "iff", NULL};

	return &provider;
}

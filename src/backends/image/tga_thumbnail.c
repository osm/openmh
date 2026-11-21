#include "backends/image/tga_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/image/decoder_backend.h"

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	(void)context;

	(void)data;
	(void)size;
	(void)options;
	(void)output;
	(void)palette;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"menu preview requires the shared indexed menu palette");
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_tga_decode_thumbnail(data, size, output, error_out);
}

const struct mh_image_decoder_provider *
mh_image_tga_thumbnail_decoder(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_image_decoder_provider provider = {
		&ops, NULL, NULL, NULL};

	return &provider;
}

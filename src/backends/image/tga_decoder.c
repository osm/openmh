#include <stdlib.h>
#include <string.h>

#include "backends/image/tga_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/image/decoder_backend.h"

static enum mh_error_code
apply_palette(struct mh_image *image, const struct mh_indexed_image *indexed,
	const struct mh_image_decode_options *options,
	struct mh_image_palette *output, struct mh_error *error_out)
{
	struct mh_image_palette palette;
	unsigned int index;
	size_t i;

	palette.count = indexed->colors;
	memcpy(palette.rgb, indexed->palette, sizeof(palette.rgb));

	if (options->palette != NULL)
		palette = *options->palette;

	for (i = 0; i < indexed->size; i++) {
		index = indexed->indices[i];

		if (index >= palette.count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"TGA pixel references an absent replacement palette entry");

		if (options->palette != NULL)
			memcpy(image->rgba + i * 4, palette.rgb + index * 3, 3);

		if (options->alpha == MH_IMAGE_ALPHA_INDEX_ZERO)
			image->rgba[i * 4 + 3] = index == 0 ? 0 : 255;
	}

	if (output != NULL)
		*output = palette;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette_output, struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_indexed_image indexed;
	struct mh_image_palette palette;
	enum mh_error_code status;

	(void)context;
	status = mh_image_tga_decode(data, size, 0, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&palette, 0, sizeof(palette));

	if (options->palette != NULL ||
		options->alpha == MH_IMAGE_ALPHA_INDEX_ZERO ||
		palette_output != NULL) {
		status = mh_image_tga_decode_indexed(
			data, size, &indexed, error_out);

		if (status == MH_ERROR_NONE) {
			status = apply_palette(
				&image, &indexed, options, &palette, error_out);
			free(indexed.indices);
		} else if (status == MH_ERROR_UNSUPPORTED &&
			options->palette == NULL &&
			options->alpha == MH_IMAGE_ALPHA_FILE)
			status = MH_ERROR_NONE;
	}

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	*output = image;

	if (palette_output != NULL)
		*palette_output = palette;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_tga_decode_indexed(data, size, output, error_out);
}

const struct mh_image_decoder_provider *
mh_image_tga_decoder(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_image_decoder_provider provider = {
		&ops, NULL, "tga", NULL};

	return &provider;
}

#include <stdlib.h>
#include <string.h>

#include "interfaces/image/decoder_backend.h"

#define PIXEL_LIMIT 16000000UL
#define EXTENSION_LIMIT 16

static enum mh_error_code
check_provider(const struct mh_image_decoder_provider *provider,
	struct mh_error *error_out)
{
	unsigned int i;
	const char *extension;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"image decoding is unavailable");

	if (provider->ops == NULL || provider->ops->rgba == NULL ||
		provider->ops->indexed == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete image decoder");

	extension = provider->extension;

	if (extension == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i <= EXTENSION_LIMIT; i++) {
		if (extension[i] == 0 && i != 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (!((extension[i] >= 'a' && extension[i] <= 'z') ||
			(extension[i] >= '0' && extension[i] <= '9')))
			break;
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "invalid image decoder extension");
}

static int
valid_size(unsigned int width, unsigned int height)
{
	return width != 0 && height != 0 && width <= PIXEL_LIMIT / height &&
		(size_t)width <= (size_t)-1 / 4 / height;
}

enum mh_error_code
mh_image_decoder_extension(const struct mh_image_decoder_provider *provider,
	const char **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_provider(provider, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing image extension output");

	*output = provider->extension;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
apply_magenta_key(struct mh_image *image)
{
	size_t i, count;
	unsigned char *pixel;

	count = (size_t)image->width * image->height;

	for (i = 0; i < count; i++) {
		pixel = image->rgba + i * 4;

		if (pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 255)
			pixel[3] = 0;
	}
}

enum mh_error_code
mh_image_decode(const struct mh_image_decoder_provider *provider,
	const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette_output, struct mh_error *error_out)
{
	struct mh_image_decode_options request;
	struct mh_image image;
	struct mh_image_palette palette;
	enum mh_error_code status;
	int magenta;

	status = check_provider(provider, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&request, 0, sizeof(request));

	if (options != NULL)
		request = *options;

	if (data == NULL || output == NULL ||
		(unsigned int)request.alpha > MH_IMAGE_ALPHA_MAGENTA ||
		(request.palette != NULL &&
			(request.palette->count == 0 ||
				request.palette->count >
					MH_IMAGE_PALETTE_COLORS)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid image decode request");

	magenta = request.alpha == MH_IMAGE_ALPHA_MAGENTA;

	if (magenta)
		request.alpha = MH_IMAGE_ALPHA_FILE;

	memset(&image, 0, sizeof(image));
	memset(&palette, 0, sizeof(palette));
	status = provider->ops->rgba(provider->context, data, size, &request,
		&image, palette_output == NULL ? NULL : &palette, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (image.rgba == NULL || !valid_size(image.width, image.height) ||
		palette.count > MH_IMAGE_PALETTE_COLORS) {
		free(image.rgba);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded image layout");
	}

	if (magenta)
		apply_magenta_key(&image);

	*output = image;

	if (palette_output != NULL)
		*palette_output = palette;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_decode_indexed(const struct mh_image_decoder_provider *provider,
	const unsigned char *data, size_t size, struct mh_indexed_image *output,
	struct mh_error *error_out)
{
	struct mh_indexed_image image;
	size_t i;
	enum mh_error_code status;

	status = check_provider(provider, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed image decode request");

	memset(&image, 0, sizeof(image));
	status = provider->ops->indexed(
		provider->context, data, size, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (image.indices == NULL || !valid_size(image.width, image.height) ||
		image.size != (size_t)image.width * image.height ||
		image.colors == 0 || image.colors > MH_IMAGE_PALETTE_COLORS) {
		free(image.indices);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded indexed image layout");
	}

	for (i = 0; i < image.size; i++)
		if (image.indices[i] >= image.colors) {
			free(image.indices);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"decoded image index is outside its palette");
		}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

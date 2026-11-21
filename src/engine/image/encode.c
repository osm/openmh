#include "interfaces/image/encoder_backend.h"

#define IMAGE_EXTENSION_MAX 8

const char *
mh_image_encoder_extension(const struct mh_image_encoder *encoder)
{
	const char *extension;
	unsigned int i;

	if (encoder == NULL || encoder->encode == NULL ||
		encoder->extension == NULL)
		return NULL;

	extension = encoder->extension;

	for (i = 0; i <= IMAGE_EXTENSION_MAX; i++) {
		if (extension[i] == 0)
			return i == 0 ? NULL : extension;

		if (!((extension[i] >= 'a' && extension[i] <= 'z') ||
			(extension[i] >= '0' && extension[i] <= '9')))
			return NULL;
	}

	return NULL;
}

enum mh_error_code
mh_image_encode(const struct mh_image_encoder *encoder,
	const struct mh_picture *picture, struct mh_file *output,
	struct mh_error *error_out)
{
	if (encoder == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"image encoding is unavailable");

	if (mh_image_encoder_extension(encoder) == NULL || picture == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid image encoding request");

	return encoder->encode(encoder->context, picture, output, error_out);
}

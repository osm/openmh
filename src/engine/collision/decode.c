#include "interfaces/collision/decoder_backend.h"

#define EXTENSION_LIMIT 16

static enum mh_error_code
check_decoder(
	const struct mh_collision_decoder *decoder, struct mh_error *error_out)
{
	const char *extension;
	unsigned int i;

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"collision decoding is unavailable");

	if (decoder->decode == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete collision decoder");

	extension = decoder->extension;

	if (extension == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i <= EXTENSION_LIMIT; i++) {
		if (extension[i] == 0 && i != 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (!((extension[i] >= 'a' && extension[i] <= 'z') ||
			(extension[i] >= '0' && extension[i] <= '9')))
			break;
	}

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"invalid collision decoder extension");
}

enum mh_error_code
mh_collision_decoder_extension(const struct mh_collision_decoder *decoder,
	const char **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing collision extension output");

	*output = decoder->extension;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_collision_decode(const struct mh_collision_decoder *decoder,
	const unsigned char *data, size_t size, struct mh_collision *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid collision decode request");

	return decoder->decode(decoder->context, data, size, output, error_out);
}

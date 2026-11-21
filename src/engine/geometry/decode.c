#include "interfaces/geometry/decoder_backend.h"

#define EXTENSION_LIMIT 16

static enum mh_error_code
check_extension(const char *extension, struct mh_error *error_out)
{
	unsigned int i;

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
		"invalid geometry decoder extension");
}

static enum mh_error_code
check_model_decoder(
	const struct mh_model_decoder *decoder, struct mh_error *error_out)
{
	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"model decoding is unavailable");

	if (decoder->decode == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete model decoder");

	return check_extension(decoder->extension, error_out);
}

enum mh_error_code
mh_model_decoder_extension(const struct mh_model_decoder *decoder,
	const char **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_model_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing model extension output");

	*output = decoder->extension;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_model_decode(const struct mh_model_decoder *decoder,
	const unsigned char *data, size_t size, struct mh_model *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_model_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid model decode request");

	return decoder->decode(decoder->context, data, size, output, error_out);
}

static enum mh_error_code
check_world_decoder(
	const struct mh_world_decoder *decoder, struct mh_error *error_out)
{
	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"world decoding is unavailable");

	if (decoder->decode == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete world decoder");

	return check_extension(decoder->extension, error_out);
}

enum mh_error_code
mh_world_decoder_extension(const struct mh_world_decoder *decoder,
	const char **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_world_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing world extension output");

	*output = decoder->extension;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_world_decode(const struct mh_world_decoder *decoder,
	const unsigned char *data, size_t size, struct mh_world *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = check_world_decoder(decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world decode request");

	return decoder->decode(decoder->context, data, size, output, error_out);
}

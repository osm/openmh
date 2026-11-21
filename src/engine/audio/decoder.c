#include <stdlib.h>

#include "interfaces/audio/decoder_backend.h"

#define DECODER_EXTENSION_MAX 8

struct mh_audio_decoder {
	const struct mh_audio_decoder_provider	*provider;
	void					*context;
	struct mh_audio_decode_info		info;
	size_t					frame_bytes, position;
};

static int
complete_provider(const struct mh_audio_decoder_provider *provider)
{
	const struct mh_audio_decoder_ops *ops;

	if (provider == NULL)
		return 0;

	ops = provider->ops;

	return ops != NULL && ops->inspect != NULL && ops->open != NULL &&
		ops->read != NULL && ops->seek != NULL && ops->close != NULL;
}

const char *
mh_audio_decoder_extension(const struct mh_audio_decoder_provider *provider)
{
	const char *extension;
	unsigned int i;

	if (!complete_provider(provider) || provider->extension == NULL)
		return NULL;

	extension = provider->extension;

	for (i = 0; i <= DECODER_EXTENSION_MAX; i++) {
		if (extension[i] == 0)
			return i == 0 ? NULL : extension;

		if (!((extension[i] >= 'a' && extension[i] <= 'z') ||
			(extension[i] >= '0' && extension[i] <= '9')))
			return NULL;
	}

	return NULL;
}

static enum mh_error_code
check_request(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	const void *output, struct mh_error *error_out)
{
	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio decoding is unavailable");

	if (!complete_provider(provider) || resources == NULL || path == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio decoding request");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_info(const struct mh_audio_decode_info *info, struct mh_error *error_out)
{
	size_t frame_bytes;

	if (info->rate == 0 || (info->channels != 1 && info->channels != 2) ||
		(info->bits != 8 && info->bits != 16))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded PCM metadata");

	frame_bytes = info->channels * (info->bits / 8);

	if (info->frames > (size_t)-1 / frame_bytes)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded PCM exceeds addressable storage");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_audio_decoder_inspect(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	struct mh_audio_decode_info *output, struct mh_error *error_out)
{
	struct mh_audio_decode_info info;
	enum mh_error_code status;

	status = check_request(provider, resources, path, output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = provider->ops->inspect(
		provider->context, resources, path, &info, error_out);

	if (status == MH_ERROR_NONE)
		status = check_info(&info, error_out);

	if (status == MH_ERROR_NONE)
		*output = info;

	return status;
}

enum mh_error_code
mh_audio_decoder_open(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	struct mh_audio_decoder **output, struct mh_error *error_out)
{
	struct mh_audio_decoder *decoder;
	enum mh_error_code status;

	status = check_request(provider, resources, path, output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening audio decoder");

	decoder->provider = provider;
	status = provider->ops->open(provider->context, resources, path,
		&decoder->context, &decoder->info, error_out);

	if (status != MH_ERROR_NONE) {
		free(decoder);
		return status;
	}

	status = check_info(&decoder->info, error_out);

	if (status != MH_ERROR_NONE) {
		mh_audio_decoder_close(decoder);
		return status;
	}

	decoder->frame_bytes =
		decoder->info.channels * (decoder->info.bits / 8);
	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_audio_decode_info *
mh_audio_decoder_info(const struct mh_audio_decoder *decoder)
{
	return decoder == NULL ? NULL : &decoder->info;
}

enum mh_error_code
mh_audio_decoder_read(struct mh_audio_decoder *decoder, void *output,
	size_t capacity, size_t requested, size_t *count,
	struct mh_error *error_out)
{
	size_t available, received;
	enum mh_error_code status;

	if (decoder == NULL || count == NULL ||
		(requested != 0 && output == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PCM read request");

	available = decoder->info.frames - decoder->position;

	if (requested > available)
		requested = available;

	if (requested > capacity / decoder->frame_bytes)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"PCM read buffer is too small");

	if (requested == 0) {
		*count = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	received = 0;
	status = decoder->provider->ops->read(decoder->provider->context,
		decoder->context, output, requested, &received, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (received == 0 || received > requested)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"audio decoder returned an invalid frame count");

	decoder->position += received;
	*count = received;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_audio_decoder_seek(struct mh_audio_decoder *decoder, size_t frame,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (decoder == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing audio decoder");

	if (frame > decoder->info.frames)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "PCM seek exceeds duration");

	status = decoder->provider->ops->seek(
		decoder->provider->context, decoder->context, frame, error_out);

	if (status == MH_ERROR_NONE)
		decoder->position = frame;

	return status;
}

void
mh_audio_decoder_close(struct mh_audio_decoder *decoder)
{
	if (decoder == NULL)
		return;

	decoder->provider->ops->close(
		decoder->provider->context, decoder->context);
	free(decoder);
}

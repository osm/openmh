#include <stdlib.h>

#include "interfaces/video/decoder_backend.h"

#define PIXEL_BYTES 4
#define FRAME_INTERVAL_MAX_US 1000000.0

struct mh_video_decoder {
	const struct mh_video_decoder_provider	*provider;
	void					*context;
	struct mh_video_decode_info		info;
	size_t					pixel_bytes, audio_frame_bytes;
	unsigned long				position;
	int					audio;
};

static enum mh_error_code
check_info(struct mh_video_decoder *decoder, struct mh_error *error_out)
{
	const struct mh_video_decode_info *info;

	info = &decoder->info;

	if (info->width == 0 || info->height == 0 || info->frames == 0 ||
		!(info->interval_us > 0 &&
			info->interval_us <= FRAME_INTERVAL_MAX_US))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded video metadata");

	if ((size_t)info->width > (size_t)-1 / PIXEL_BYTES / info->height)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded video pixels exceed addressable storage");

	decoder->pixel_bytes = (size_t)info->width * info->height * PIXEL_BYTES;

	if (info->audio_rate == 0 && info->audio_channels == 0 &&
		info->audio_bits == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (info->audio_rate == 0 ||
		(info->audio_channels != 1 && info->audio_channels != 2) ||
		(info->audio_bits != 8 && info->audio_bits != 16))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded video audio metadata");

	decoder->audio_frame_bytes =
		info->audio_channels * (info->audio_bits / 8);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_video_decoder_open(const struct mh_video_decoder_provider *provider,
	const unsigned char *data, size_t size, int audio,
	struct mh_video_decoder **output, struct mh_error *error_out)
{
	struct mh_video_decoder *decoder;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"video decoding is unavailable");

	if (provider->ops == NULL || provider->ops->open == NULL ||
		provider->ops->read == NULL || provider->ops->close == NULL ||
		data == NULL || size == 0 || output == NULL ||
		(audio != 0 && audio != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid video decoding request");

	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate video decoder");

	decoder->provider = provider;
	decoder->audio = audio;
	status = provider->ops->open(provider->context, data, size, audio,
		&decoder->context, &decoder->info, error_out);

	if (status != MH_ERROR_NONE) {
		free(decoder);
		return status;
	}

	status = check_info(decoder, error_out);

	if (status != MH_ERROR_NONE) {
		mh_video_decoder_close(decoder);
		return status;
	}

	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_video_decode_info *
mh_video_decoder_info(const struct mh_video_decoder *decoder)
{
	return decoder == NULL ? NULL : &decoder->info;
}

enum mh_error_code
mh_video_decoder_read(struct mh_video_decoder *decoder,
	struct mh_video_frame *output, int *available,
	struct mh_error *error_out)
{
	struct mh_video_frame frame;
	enum mh_error_code status;

	if (decoder == NULL || output == NULL || available == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid video frame request");

	if (decoder->position == decoder->info.frames) {
		*available = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = decoder->provider->ops->read(decoder->provider->context,
		decoder->context, &frame, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (frame.rgba == NULL || frame.rgba_bytes != decoder->pixel_bytes ||
		(frame.pcm_bytes != 0 &&
			(frame.pcm == NULL || !decoder->audio ||
				decoder->audio_frame_bytes == 0 ||
				frame.pcm_bytes % decoder->audio_frame_bytes !=
					0)))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"invalid decoded video frame");

	decoder->position++;
	*output = frame;
	*available = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_video_decoder_close(struct mh_video_decoder *decoder)
{
	if (decoder == NULL)
		return;

	decoder->provider->ops->close(
		decoder->provider->context, decoder->context);
	free(decoder);
}

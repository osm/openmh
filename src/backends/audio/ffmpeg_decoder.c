#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_decode.h"
#include "backends/audio/ffmpeg_provider.h"
#include "interfaces/audio/decoder_backend.h"

struct decoded_audio {
	unsigned char	*pcm;
	size_t		frame_bytes, position;
};

static void
close_decoder(void *context, void *token)
{
	struct decoded_audio *decoder;

	(void)context;
	decoder = token;
	free(decoder->pcm);
	free(decoder);
}

static enum mh_error_code
inspect(void *context, const struct mh_resource_source *resources,
	const char *path, struct mh_audio_decode_info *output,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_input *input;
	enum mh_error_code status;

	(void)context;
	input = NULL;
	status = mh_ffmpeg_input_resource(
		resources, path, "ogg", "opus", &input, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_opus_info(input, output, error_out);

	mh_ffmpeg_input_close(input);

	return status;
}

static enum mh_error_code
open_decoder(void *context, const struct mh_resource_source *resources,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_input *input;
	struct decoded_audio *decoder;
	struct mh_audio_decode_info metadata;
	enum mh_error_code status;

	(void)context;
	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate audio decoder");

	input = NULL;
	status = mh_ffmpeg_input_resource(
		resources, path, "ogg", "opus", &input, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_opus_info(input, &metadata, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_opus_decode(
			input, &metadata, &decoder->pcm, error_out);

	mh_ffmpeg_input_close(input);

	if (status != MH_ERROR_NONE) {
		close_decoder(NULL, decoder);
		return status;
	}

	decoder->frame_bytes = metadata.channels * 2;
	*info = metadata;
	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_decoder(void *context, void *token, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	struct decoded_audio *decoder;

	(void)context;
	decoder = token;
	memcpy(output, decoder->pcm + decoder->position * decoder->frame_bytes,
		requested * decoder->frame_bytes);
	decoder->position += requested;
	*count = requested;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
seek_decoder(
	void *context, void *token, size_t frame, struct mh_error *error_out)
{
	struct decoded_audio *decoder;

	(void)context;
	decoder = token;
	decoder->position = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_audio_decoder_provider *
mh_audio_ffmpeg_decoder(void)
{
	static const struct mh_audio_decoder_ops ops = {inspect, open_decoder,
		read_decoder, seek_decoder, close_decoder};

	static const struct mh_audio_decoder_provider provider = {
		"opus", &ops, NULL, NULL};

	return &provider;
}

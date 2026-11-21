#include <stdlib.h>
#include <string.h>

#include "backends/video/ffmpeg_internal.h"
#include "backends/video/ffmpeg_provider.h"
#include "interfaces/video/decoder_backend.h"

static void
close_decoder(void *context, void *token)
{
	struct mh_ffmpeg_video *decoder;
	size_t i;

	(void)context;
	decoder = token;

	for (i = 0; i < decoder->count; i++)
		av_packet_free(&decoder->packets[i].packet);

	free(decoder->packets);
	av_frame_free(&decoder->frame);
	avcodec_free_context(&decoder->codec);
	sws_freeContext(decoder->conversion);
	free(decoder->rgba);
	free(decoder->audio.data);
	free(decoder);
}

static enum mh_error_code
open_decoder(void *context, const unsigned char *data, size_t size, int audio,
	void **output, struct mh_video_decode_info *info,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_video *decoder;
	struct mh_ffmpeg_input *input;
	enum mh_error_code status;

	(void)context;
	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie decoder");

	decoder->smacker = size >= 4 &&
		(memcmp(data, "SMK2", 4) == 0 || memcmp(data, "SMK4", 4) == 0);
	input = NULL;
	status = mh_ffmpeg_input_memory(data, size,
		decoder->smacker ? "smk" : "matroska",
		decoder->smacker ? "smackvid,smackaud,pcm_u8,pcm_s16le"
				: "vp9,opus",
		&input, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_video_prepare(
			decoder, input, audio, error_out);

	mh_ffmpeg_input_close(input);

	if (status != MH_ERROR_NONE) {
		close_decoder(NULL, decoder);
		return status;
	}

	*output = decoder;
	*info = decoder->info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_decoder(void *context, void *token, struct mh_video_frame *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_ffmpeg_video_read(token, output, error_out);
}

const struct mh_video_decoder_provider *
mh_video_ffmpeg_decoder(void)
{
	static const struct mh_video_decoder_ops ops = {
		open_decoder, read_decoder, close_decoder};

	static const struct mh_video_decoder_provider provider = {&ops, NULL};

	return &provider;
}

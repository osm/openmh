#include <stdlib.h>
#include <string.h>

#include "backends/audio/wave_metadata.h"
#include "backends/audio/wave_provider.h"
#include "engine/assets/wave.h"
#include "interfaces/audio/decoder_backend.h"
#include "interfaces/storage/resource.h"

struct wave_decoder {
	struct mh_file		file;
	struct mh_audio_pcm	pcm;
	size_t			position;
};

static void
wave_close(void *context, void *token)
{
	struct wave_decoder *decoder;

	(void)context;
	decoder = token;
	free(decoder->file.data);
	free(decoder);
}

static enum mh_error_code
wave_open(void *context, const struct mh_resource_source *resources,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	struct wave_decoder *decoder;
	enum mh_error_code status;

	(void)context;
	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening WAVE decoder");

	status = mh_resource_source_read(
		resources, path, &decoder->file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_audio_wave_decode(decoder->file.data,
			decoder->file.size, &decoder->pcm, error_out);

	if (status != MH_ERROR_NONE) {
		wave_close(NULL, decoder);
		return status;
	}

	info->frames = decoder->pcm.frames;
	info->rate = decoder->pcm.rate;
	info->channels = decoder->pcm.channels;
	info->bits = decoder->pcm.bits;
	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
wave_read(void *context, void *token, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	struct wave_decoder *decoder;
	size_t frame_bytes;

	(void)context;
	decoder = token;
	frame_bytes = decoder->pcm.channels * (decoder->pcm.bits / 8);
	memcpy(output, decoder->pcm.pcm + decoder->position * frame_bytes,
		requested * frame_bytes);
	decoder->position += requested;
	*count = requested;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
wave_seek(void *context, void *token, size_t frame, struct mh_error *error_out)
{
	struct wave_decoder *decoder;

	(void)context;
	decoder = token;
	decoder->position = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_audio_decoder_provider *
mh_audio_wave_decoder(void)
{
	static const struct mh_audio_decoder_ops ops = {mh_audio_wave_inspect,
		wave_open, wave_read, wave_seek, wave_close};

	static const struct mh_audio_decoder_provider provider = {
		"wav", &ops, NULL, NULL};

	return &provider;
}

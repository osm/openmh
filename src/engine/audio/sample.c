#include <stdlib.h>

#include "engine/audio/sample.h"
#include "interfaces/audio/decoder.h"

#define SAMPLE_BLOCK_FRAMES 4096

static enum mh_error_code
read_sample(struct mh_audio_decoder *decoder, struct mh_file *storage,
	struct mh_audio_pcm *pcm, struct mh_error *error_out)
{
	size_t frame_bytes, position, requested, count;
	enum mh_error_code status;

	frame_bytes = pcm->channels * (pcm->bits / 8);
	position = 0;

	while (position < pcm->frames) {
		requested = pcm->frames - position;

		if (requested > SAMPLE_BLOCK_FRAMES)
			requested = SAMPLE_BLOCK_FRAMES;

		status = mh_audio_decoder_read(decoder,
			storage->data + position * frame_bytes,
			storage->size - position * frame_bytes, requested,
			&count, NULL);

		if (status != MH_ERROR_NONE)
			return mh_error_set(error_out, status,
				"cannot decode audio sample");

		position += count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_audio_sample_load_from(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	size_t limit, struct mh_file *storage, struct mh_audio_pcm *output,
	struct mh_error *error_out)
{
	struct mh_audio_decoder *decoder;
	const struct mh_audio_decode_info *info;
	struct mh_file bytes;
	struct mh_audio_pcm pcm;
	size_t frame_bytes;
	enum mh_error_code status;

	if (storage == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing audio sample output");

	status = mh_audio_decoder_open(
		provider, resources, path, &decoder, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	info = mh_audio_decoder_info(decoder);
	frame_bytes = info->channels * (info->bits / 8);

	if (info->frames == 0 || info->frames > limit / frame_bytes) {
		mh_audio_decoder_close(decoder);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded audio sample is empty or exceeds its limit");
	}

	pcm.frames = info->frames;
	pcm.rate = info->rate;
	pcm.channels = info->channels;
	pcm.bits = info->bits;
	bytes.size = pcm.bytes = info->frames * frame_bytes;
	bytes.data = malloc(bytes.size);

	if (bytes.data == NULL) {
		mh_audio_decoder_close(decoder);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading audio sample");
	}

	pcm.pcm = bytes.data;
	status = read_sample(decoder, &bytes, &pcm, error_out);
	mh_audio_decoder_close(decoder);

	if (status != MH_ERROR_NONE) {
		free(bytes.data);
		return status;
	}

	*storage = bytes;
	*output = pcm;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

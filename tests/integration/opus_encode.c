#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_encode.h"
#include "backends/audio/ffmpeg_provider.h"
#include "backends/audio/wave_provider.h"
#include "backends/storage/filesystem_provider.h"

static enum mh_error_code
cancel_encoding(void *context, struct mh_error *error_out)
{
	int *calls;

	calls = context;
	(*calls)++;

	if (*calls == 2)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "test cancellation");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
verify_playback(const char *path)
{
	struct mh_audio_decoder *decoder;
	const struct mh_audio_decode_info *info;
	struct mh_error error;
	unsigned char pcm[4096 * 4];
	size_t count, total;
	enum mh_error_code status;

	status = mh_audio_decoder_open(mh_audio_ffmpeg_decoder(),
		mh_filesystem_resources(), path, &decoder, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 0;
	}

	total = 0;

	do {
		status = mh_audio_decoder_read(
			decoder, pcm, sizeof(pcm), 4096, &count, &error);

		if (status == MH_ERROR_NONE)
			total += count;
	} while (status == MH_ERROR_NONE && count != 0);

	info = mh_audio_decoder_info(decoder);

	if (status == MH_ERROR_NONE && total != info->frames)
		status = mh_error_set(
			&error, MH_ERROR_INVALID, "decoded duration mismatch");

	if (status == MH_ERROR_NONE)
		printf("%lu %lu %u\n", (unsigned long)total, info->rate,
			info->channels);
	else
		fprintf(stderr, "%s\n", error.message);

	mh_audio_decoder_close(decoder);

	return status == MH_ERROR_NONE;
}

int
main(int argc, char *argv[])
{
	struct mh_audio_decoder *decoder;
	struct mh_file output;
	struct mh_error error;
	unsigned char sentinel;
	FILE *file;
	enum mh_error_code status;
	int cancel, calls, ok;

	if (argc != 3 && argc != 4)
		return 1;

	if (!mh_audio_ffmpeg_encoding_available()) {
		fprintf(stderr,
			"FFmpeg libopus encoder or Ogg muxer is unavailable\n");
		return 77;
	}

	cancel = argc == 4 && strcmp(argv[3], "cancel") == 0;
	calls = 0;
	status = mh_audio_decoder_open(mh_audio_wave_decoder(),
		mh_filesystem_resources(), argv[1], &decoder, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	output.data = &sentinel;
	output.size = 123;
	status = mh_audio_ffmpeg_encode(decoder,
		cancel ? cancel_encoding : NULL, &calls, &output, &error);
	mh_audio_decoder_close(decoder);

	if (cancel)
		return !(status == MH_ERROR_FAILED && calls == 2 &&
			output.data == &sentinel && output.size == 123);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	file = fopen(argv[2], "wb");
	ok = file != NULL;

	if (ok)
		ok = fwrite(output.data, 1, output.size, file) == output.size;

	if (file != NULL && fclose(file) != 0)
		ok = 0;

	free(output.data);

	return !(ok && verify_playback(argv[2]));
}

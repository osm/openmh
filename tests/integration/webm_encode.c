#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/video/ffmpeg_encode.h"
#include "backends/video/ffmpeg_provider.h"

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

int
main(int argc, char *argv[])
{
	struct mh_video_decoder *decoder;
	struct mh_file input, output;
	struct mh_error error;
	unsigned char sentinel;
	FILE *file;
	enum mh_error_code status;
	int cancel, calls, ok;

	if (argc != 3 && argc != 4)
		return 1;

	if (!mh_video_ffmpeg_encoding_available()) {
		fprintf(stderr,
			"FFmpeg VP9/Opus encoder or WebM muxer is unavailable\n");
		return 77;
	}

	cancel = argc == 4 && strcmp(argv[3], "cancel") == 0;
	calls = 0;
	decoder = NULL;
	status = mh_resource_source_read(
		mh_filesystem_resources(), argv[1], &input, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_video_decoder_open(mh_video_ffmpeg_decoder(),
			input.data, input.size, 1, &decoder, &error);
		free(input.data);
	}

	output.data = &sentinel;
	output.size = 123;

	if (status == MH_ERROR_NONE)
		status = mh_video_ffmpeg_encode(decoder,
			cancel ? cancel_encoding : NULL, &calls, &output,
			&error);

	mh_video_decoder_close(decoder);

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

	return !ok;
}

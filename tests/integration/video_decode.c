/* Decoder qualification tool; never invokes an external playback process. */
#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/video/ffmpeg_provider.h"
#include "interfaces/video/decoder.h"

static int
write_picture(
	FILE *output, const struct mh_video_frame *frame, unsigned char *rgb)
{
	size_t i, pixels;

	pixels = frame->rgba_bytes / 4;

	for (i = 0; i < pixels; i++) {
		rgb[i * 3] = frame->rgba[i * 4];
		rgb[i * 3 + 1] = frame->rgba[i * 4 + 1];
		rgb[i * 3 + 2] = frame->rgba[i * 4 + 2];
	}

	return fwrite(rgb, 3, pixels, output) == pixels;
}

static int
decode_frames(struct mh_video_decoder *decoder, FILE *video, FILE *audio,
	unsigned long *audio_bytes)
{
	const struct mh_video_decode_info *info;
	struct mh_video_frame frame;
	unsigned char *rgb;
	int available, ok;

	info = mh_video_decoder_info(decoder);
	rgb = malloc((size_t)info->width * info->height * 3);

	if (rgb == NULL)
		return 0;

	ok = 1;
	*audio_bytes = 0;

	for (;;) {
		if (mh_video_decoder_read(decoder, &frame, &available, NULL) !=
			MH_ERROR_NONE) {
			ok = 0;
			break;
		}

		if (!available)
			break;

		if ((video != NULL && !write_picture(video, &frame, rgb)) ||
			(audio != NULL && frame.pcm_bytes != 0 &&
				fwrite(frame.pcm, 1, frame.pcm_bytes, audio) !=
					frame.pcm_bytes)) {
			ok = 0;
			break;
		}

		*audio_bytes += (unsigned long)frame.pcm_bytes;
	}

	free(rgb);

	return ok;
}

int
main(int argc, char *argv[])
{
	struct mh_video_decoder *decoder;
	const struct mh_video_decode_info *info;
	struct mh_file input;
	struct mh_error error;
	FILE *video, *audio;
	unsigned long audio_bytes;
	enum mh_error_code status;
	int ok;

	if (argc != 2 && argc != 4) {
		fprintf(stderr, "usage: movie-decode FILE [RGB24 PCM]\n");
		return 1;
	}

	audio_bytes = 0;
	decoder = NULL;
	status = mh_resource_source_read(
		mh_filesystem_resources(), argv[1], &input, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_video_decoder_open(mh_video_ffmpeg_decoder(),
			input.data, input.size, 1, &decoder, &error);
		free(input.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	video = audio = NULL;

	if (argc == 4) {
		video = fopen(argv[2], "wb");
		audio = fopen(argv[3], "wb");
	}

	ok = argc == 2 || (video != NULL && audio != NULL);

	if (ok)
		ok = decode_frames(decoder, video, audio, &audio_bytes);

	if (video != NULL && fclose(video) != 0)
		ok = 0;

	if (audio != NULL && fclose(audio) != 0)
		ok = 0;

	if (ok) {
		info = mh_video_decoder_info(decoder);
		printf("%ux%u %lu frames %.9g us/frame stereo16 %lu Hz %lu audio bytes\n",
			info->width, info->height, info->frames,
			info->interval_us, info->audio_rate, audio_bytes);
	}

	mh_video_decoder_close(decoder);

	return !ok;
}

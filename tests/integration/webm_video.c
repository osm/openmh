#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "backends/video/divi_provider.h"
#include "backends/video/ffmpeg_provider.h"
#include "interfaces/video/decoder.h"
#include "interfaces/video/source.h"

static enum mh_error_code
read_clip(const char *path, struct mh_file *output, struct mh_error *error_out)
{
	const struct mh_resource_archive_provider *zip;
	const struct mh_resource_source *resources;
	char collection[2048];
	size_t length;
	enum mh_error_code status;

	length = strlen(path);

	if (length < 4 || strcmp(path + length - 4, ".ass") != 0)
		return mh_resource_source_read(
			mh_filesystem_resources(), path, output, error_out);

	if (length + sizeof("/Movies/movies.pak") > sizeof(collection))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"test package path is too long");

	zip = mh_zip_resource_provider();
	status = zip->open(
		mh_filesystem_resources(), path, &resources, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sprintf(collection, "%s/Movies/movies.pak", path);
	status = mh_video_source_read(mh_video_divi_source(), resources,
		collection, 1, 0, output, error_out);
	zip->close(resources);

	return status;
}

static int
dump_frames(struct mh_video_decoder *decoder, FILE *pictures, FILE *sound,
	unsigned long *count, unsigned long *audio_bytes)
{
	struct mh_video_frame frame;
	struct mh_error error;
	int available;

	*count = *audio_bytes = 0;

	for (;;) {
		if (mh_video_decoder_read(decoder, &frame, &available,
			&error) != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 0;
		}

		if (!available)
			return 1;

		if (fwrite(frame.rgba, 1, frame.rgba_bytes, pictures) !=
				frame.rgba_bytes ||
			(frame.pcm_bytes != 0 &&
				fwrite(frame.pcm, 1, frame.pcm_bytes, sound) !=
					frame.pcm_bytes))
			return 0;

		(*count)++;
		*audio_bytes += (unsigned long)frame.pcm_bytes;
	}
}

int
main(int argc, char *argv[])
{
	struct mh_video_decoder *decoder;
	const struct mh_video_decode_info *info;
	struct mh_file file;
	struct mh_error error;
	FILE *pictures, *sound;
	unsigned long count, audio_bytes;
	int ok;

	if (argc != 5)
		return 1;

	if (read_clip(argv[1], &file, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	decoder = NULL;
	ok = mh_video_decoder_open(mh_video_ffmpeg_decoder(), file.data,
		file.size, atoi(argv[4]), &decoder,
		&error) == MH_ERROR_NONE;
	free(file.data);

	if (!ok) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	pictures = fopen(argv[2], "wb");
	sound = fopen(argv[3], "wb");
	ok = pictures != NULL && sound != NULL;

	if (ok)
		ok = dump_frames(
			decoder, pictures, sound, &count, &audio_bytes);

	if (pictures != NULL && fclose(pictures) != 0)
		ok = 0;

	if (sound != NULL && fclose(sound) != 0)
		ok = 0;

	if (ok) {
		info = mh_video_decoder_info(decoder);
		printf("%u %u %lu %.12g %lu %u %u %lu %lu\n", info->width,
			info->height, info->frames, info->interval_us,
			info->audio_rate, info->audio_channels,
			info->audio_bits, count, audio_bytes);
		ok = count == info->frames;
	}

	mh_video_decoder_close(decoder);

	return !ok;
}

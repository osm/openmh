#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_provider.h"
#include "backends/audio/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/zip_provider.h"
#include "engine/audio/music_asset.h"
#include "engine/audio/music_catalog.h"
#include "engine/audio/sample_path.h"
#include "engine/core/binary.h"
#include "interfaces/audio/decoder.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "Opus test:%d: %s\n", __LINE__,        \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

/* Independent decoder versions can round a sample one integer unit apart. */
static int
same_pcm(const unsigned char *actual, const unsigned char *reference,
	size_t size)
{
	size_t i;
	long a, b, difference;

	for (i = 0; i < size; i += 2) {
		a = (long)mh_read_u16le(actual + i);
		b = (long)mh_read_u16le(reference + i);

		if (a >= 32768)
			a -= 65536;

		if (b >= 32768)
			b -= 65536;

		difference = a - b;

		if (difference < -1 || difference > 1) {
			fprintf(stderr, "PCM sample %lu differs by %ld\n",
				(unsigned long)(i / 2), difference);
			return 0;
		}
	}

	return 1;
}

static int
check_music(const struct mh_resource_source *resources, const char *root,
	const struct mh_file *reference)
{
	struct mh_music_catalog catalog;
	struct mh_music_asset asset;
	char directory[2048];

	CHECK(strlen(root) + sizeof("/Music") <= sizeof(directory));
	sprintf(directory, "%s/Music", root);
	memset(&catalog, 0, sizeof(catalog));
	CHECK(mh_music_catalog_scan_from(mh_audio_fallback_decoder(), resources,
		root, NULL, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.last_track == 4);
	CHECK(catalog.seconds[2] == 2 && catalog.seconds[3] == 2 &&
		catalog.seconds[4] == 2);
	CHECK(mh_music_asset_load_from(mh_audio_fallback_decoder(), resources,
		directory, 2, &asset, NULL) == MH_ERROR_NONE);
	CHECK(asset.wave.rate == 48000 && asset.wave.frames == 96000);
	CHECK(asset.file.size == reference->size);
	CHECK(same_pcm(asset.file.data, reference->data, reference->size));
	mh_music_asset_free(&asset);

	/*
	 * A mixed installation keeps original WAV tracks, including precedence.
	 */
	CHECK(mh_music_asset_load_from(mh_audio_fallback_decoder(), resources,
		directory, 3, &asset, NULL) == MH_ERROR_NONE);
	CHECK(asset.wave.rate == 44100 && asset.wave.frames == 88200);
	mh_music_asset_free(&asset);
	CHECK(mh_music_asset_load_from(mh_audio_fallback_decoder(), resources,
		directory, 4, &asset, NULL) == MH_ERROR_NONE);
	CHECK(asset.wave.rate == 44100 && asset.wave.frames == 88200);
	mh_music_asset_free(&asset);

	return 0;
}

static int
check_seeking(const struct mh_resource_source *resources, const char *root,
	const struct mh_file *reference)
{
	static const size_t positions[] = {0, 1, 47999, 95999, 96000, 12345};
	struct mh_audio_decoder *decoder;
	struct mh_audio_decode_info info;
	unsigned char samples[128];
	char path[2048];
	size_t i, count, expected;

	CHECK(mh_audio_sample_path_from(mh_audio_fallback_decoder(), resources,
		root, "Music/track-02.wav", path, sizeof(path),
		NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_inspect(mh_audio_ffmpeg_decoder(), resources,
		path, &info, NULL) == MH_ERROR_NONE);
	CHECK(info.rate == 48000 && info.frames == 96000 &&
		info.channels == 2 && info.bits == 16);
	CHECK(mh_audio_decoder_open(mh_audio_ffmpeg_decoder(), resources, path,
		&decoder, NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
		CHECK(mh_audio_decoder_seek(decoder, positions[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_audio_decoder_seek(decoder, 96001, NULL) ==
			MH_ERROR_RANGE);
		CHECK(mh_audio_decoder_read(decoder, samples, sizeof(samples),
			32, &count, NULL) == MH_ERROR_NONE);
		expected = 96000 - positions[i];

		if (expected > 32)
			expected = 32;

		CHECK(count == expected);
		CHECK(same_pcm(samples, reference->data + positions[i] * 4,
			count * 4));
	}

	mh_audio_decoder_close(decoder);

	return 0;
}

static int
check_invalid(const char *root)
{
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_audio_decoder *decoder;
	struct mh_audio_decode_info info;
	struct mh_file file;
	char path[2048];

	CHECK(strlen(root) + sizeof("/Music/track-02.opus") <= sizeof(path));
	sprintf(path, "%s/Music/track-02.opus", root);
	CHECK(mh_resource_source_read(mh_filesystem_resources(), path, &file,
		NULL) == MH_ERROR_NONE);
	entry.path = "broken.opus";
	entry.data = file.data;
	entry.size = 20;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	decoder = NULL;
	memset(&info, 0, sizeof(info));
	info.frames = 42;
	CHECK(mh_audio_decoder_inspect(mh_audio_ffmpeg_decoder(), resources,
		entry.path, &info, NULL) != MH_ERROR_NONE);
	CHECK(info.frames == 42);
	CHECK(mh_audio_decoder_open(mh_audio_ffmpeg_decoder(), resources,
		entry.path, &decoder, NULL) != MH_ERROR_NONE);
	CHECK(decoder == NULL);
	mh_memory_resources_close(resources);
	free(file.data);

	return 0;
}

int
main(int argc, char *argv[])
{
	const struct mh_resource_archive_provider *zip;
	const struct mh_resource_source *resources;
	struct mh_file reference;

	CHECK(argc == 4);
	CHECK(mh_resource_source_read(mh_filesystem_resources(), argv[3],
		&reference, NULL) == MH_ERROR_NONE);
	CHECK(reference.size == 96000 * 4);
	CHECK(check_music(mh_filesystem_resources(), argv[1], &reference) == 0);
	CHECK(check_seeking(mh_filesystem_resources(), argv[1], &reference) ==
		0);
	CHECK(check_invalid(argv[1]) == 0);
	zip = mh_zip_resource_provider();
	CHECK(zip->open(mh_filesystem_resources(), argv[2], &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_music(resources, argv[2], &reference) == 0);
	CHECK(check_seeking(resources, argv[2], &reference) == 0);
	zip->close(resources);
	free(reference.data);
	puts("Opus duration, PCM, seeking, mixed music and package playback passed");

	return 0;
}

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/archive/disc.h"
#include "engine/assets/prepare_cd_audio.h"
#include "engine/assets/prepare_cue.h"
#include "engine/assets/prepare_disc_layout.h"
#include "engine/core/binary.h"
#include "interfaces/storage/data_directory.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
check_sectors(void)
{
	unsigned char raw[3 * MH_DISC_SECTOR_BYTES];
	unsigned char logical[3 * MH_DISC_BLOCK_BYTES], output[4095];
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;
	struct mh_disc_image image;
	struct mh_error error;
	size_t i, sector;
	int mode;

	memset(raw, 0, sizeof(raw));

	for (i = 0; i < sizeof(logical); i++)
		logical[i] = (unsigned char)(i % 251);

	for (sector = 0; sector < 3; sector++) {
		i = sector * MH_DISC_SECTOR_BYTES;
		memset(raw + i + 1, 255, 10);
		raw[i + 15] = 1;
		memcpy(raw + i + MH_DISC_DATA_OFFSET,
			logical + sector * MH_DISC_BLOCK_BYTES,
			MH_DISC_BLOCK_BYTES);
	}

	entries[0].path = "raw";
	entries[0].data = raw;
	entries[0].size = sizeof(raw);
	entries[1].path = "iso";
	entries[1].data = logical;
	entries[1].size = sizeof(logical);
	check(mh_memory_resources_open(entries, 2, &source, &error) ==
			MH_ERROR_NONE,
		"open sector resources");

	if (failures != 0)
		return;

	for (mode = 0; mode < 2; mode++) {
		check(mh_disc_image_init(source, mode ? "raw" : "iso",
			mode ? MH_DISC_MODE1 : MH_DISC_ISO, &image,
			&error) == MH_ERROR_NONE,
			"open logical/raw image");
		check(mh_disc_image_read(&image, 2047, output, sizeof(output),
			&error) == MH_ERROR_NONE &&
				memcmp(output, logical + 2047,
					sizeof(output)) == 0,
			"partial read crosses two sector boundaries without headers");
		check(mh_disc_image_read(&image, sizeof(logical) - 1, output, 2,
			&error) == MH_ERROR_RANGE,
			"refuse read beyond image");
		check(mh_disc_image_init_track(source, mode ? "raw" : "iso",
			mode ? MH_DISC_MODE1 : MH_DISC_ISO,
			2UL *
				(mode ? MH_DISC_SECTOR_BYTES
					: MH_DISC_BLOCK_BYTES),
			&image, &error) == MH_ERROR_NONE &&
				image.blocks == 2,
			"bound data track before following audio sectors");
		check(mh_disc_image_read(&image, 4095, output, 2, &error) ==
				MH_ERROR_RANGE,
			"data read cannot enter next track");
		check(mh_disc_image_init_track(source, mode ? "raw" : "iso",
			mode ? MH_DISC_MODE1 : MH_DISC_ISO, ULONG_MAX,
			&image, &error) == MH_ERROR_RANGE &&
				image.blocks == 2,
			"truncated data track preserves previous image");
	}

	mh_memory_resources_close(source);
	raw[MH_DISC_SECTOR_BYTES + 15] = 2;
	check(mh_memory_resources_open(entries, 2, &source, &error) ==
			MH_ERROR_NONE,
		"open invalid-sector fixture");

	if (failures != 0)
		return;

	check(mh_disc_image_init(source, "raw", MH_DISC_MODE1, &image,
		&error) == MH_ERROR_NONE,
		"open raw fixture before checking sector headers");
	check(mh_disc_image_read(&image, 2047, output, 2, &error) ==
			MH_ERROR_INVALID,
		"reject unsupported raw sector mode");
	mh_memory_resources_close(source);
}

static void
check_cue(void)
{
	char text[4096], *cursor, *track;
	struct mh_cue cue, before;
	struct mh_disc_layout layout;
	struct mh_error error;
	unsigned int i;

	cursor = text;

	for (i = 1; i <= 11; i++) {
		sprintf(cursor,
			"FILE \"Track %02u.bin\" BINARY\r\n"
			"  TRACK %02u %s\r\n  INDEX 01 00:00:00\r\n",
			i, i, i == 1 ? "MODE1/2352" : "AUDIO");
		cursor += strlen(cursor);
	}

	check(mh_cue_parse(text, strlen(text), &cue, &error) == MH_ERROR_NONE &&
			strcmp(cue.files[0], "Track 01.bin") == 0 &&
			strcmp(cue.files[10], "Track 11.bin") == 0,
		"read separate data/audio files with CRLF and spaces");
	memset(&layout, 0, sizeof(layout));
	check(mh_disc_layout_parse(text, strlen(text), &layout, &error) ==
				MH_ERROR_NONE &&
			layout.image_size == 0 &&
			layout.tracks[0].length == 0 &&
			layout.tracks[10].offset == 0 &&
			!layout.big_endian_audio &&
			layout.data_format == MH_DISC_MODE1 &&
			strcmp(layout.tracks[10].file, "Track 11.bin") == 0,
		"separate CUE files retain raw data and little-endian audio");
	before = cue;
	track = strstr(text, "INDEX 01 00:00:00");
	track[strlen("INDEX 01 00:00:0")] = '1';
	check(mh_cue_parse(text, strlen(text), &cue, &error) ==
				MH_ERROR_UNSUPPORTED &&
			memcmp(&cue, &before, sizeof(cue)) == 0,
		"unsupported index preserves previous cue");
	check(mh_cue_parse("FILE \"../outside.bin\" BINARY", 28, &cue,
		&error) != MH_ERROR_NONE,
		"refuse track path traversal");
}

static void
check_audio_range(const struct mh_resource_source *source,
	const struct mh_user_data *data, struct mh_data_directory *directory,
	const unsigned char *samples)
{
	struct mh_file file;
	struct mh_error error;
	enum mh_error_code status;
	size_t i;
	int matches;

	/* Starts after cooked data; crosses the 64 KiB copy buffer boundary. */
	status = mh_cd_audio_extract_track(source, "audio.bin", 4096,
		28UL * MH_DISC_SECTOR_BYTES, 1, directory, 3, &error);
	check(status == MH_ERROR_NONE, "extract bounded big-endian track");

	if (status != MH_ERROR_NONE)
		return;

	status = mh_user_data_read(
		data, "audio/track-03.wav", 100000, &file, &error);
	check(status == MH_ERROR_NONE, "read converted big-endian track");

	if (status == MH_ERROR_NONE) {
		matches = file.size == 44 + 28UL * MH_DISC_SECTOR_BYTES &&
			mh_read_u32le(file.data + 40) ==
				28UL * MH_DISC_SECTOR_BYTES;

		for (i = 44; matches && i < file.size; i += 2)
			matches = file.data[i] == samples[4096 + i - 44 + 1] &&
				file.data[i + 1] == samples[4096 + i - 44];

		check(matches,
			"swap every PCM sample without including neighboring tracks");
		free(file.data);
	}

	check(mh_cd_audio_extract_track(source, "audio.bin", 4096,
		30UL * MH_DISC_SECTOR_BYTES, 1, directory, 4,
		&error) == MH_ERROR_RANGE,
		"reject truncated audio track");
	check(mh_cd_audio_extract_track(source, "audio.bin", ULONG_MAX, 2352, 1,
		directory, 4, &error) == MH_ERROR_RANGE,
		"reject overflowing audio range");
	check(mh_cd_audio_extract_track(source, "audio.bin", 4096, 2351, 1,
		directory, 4, &error) == MH_ERROR_RANGE,
		"reject partial audio sector");
	check(mh_user_data_read(data, "audio/track-04.wav", 100, &file,
		&error) == MH_ERROR_NOT_FOUND,
		"invalid range publishes no audio");
}

static void
check_audio(void)
{
	unsigned char samples[30 * MH_DISC_SECTOR_BYTES];
	struct mh_resource_entry entry;
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_file file;
	struct mh_error error;
	size_t i;
	enum mh_error_code status;

	for (i = 0; i < sizeof(samples); i++)
		samples[i] = (unsigned char)(i % 253);

	entry.path = "audio.bin";
	entry.data = samples;
	entry.size = sizeof(samples);
	source = NULL;
	data = NULL;
	directory = NULL;
	status = mh_memory_resources_open(&entry, 1, &source, &error);

	if (status == MH_ERROR_NONE)
		status = mh_memory_user_data_open(&data, &error);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(data, "audio", &error);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_open_directory(
			data, "audio", "resources", &directory, &error);

	if (status == MH_ERROR_NONE)
		status = mh_cd_audio_extract(
			source, entry.path, directory, 2, &error);

	check(status == MH_ERROR_NONE, "stream audio through selected storage");

	if (status == MH_ERROR_NONE) {
		status = mh_user_data_read(data, "audio/track-02.wav",
			sizeof(samples) + 44, &file, &error);
		check(status == MH_ERROR_NONE, "read wrapped track");

		if (status == MH_ERROR_NONE) {
			check(file.size == sizeof(samples) + 44 &&
					memcmp(file.data + 44, samples,
						sizeof(samples)) == 0,
				"preserve samples across copy chunk boundary");
			check(memcmp(file.data, "RIFF", 4) == 0 &&
					memcmp(file.data + 8, "WAVEfmt ", 8) ==
						0 &&
					mh_read_u32le(file.data + 4) ==
						sizeof(samples) + 36 &&
					mh_read_u16le(file.data + 20) == 1 &&
					mh_read_u16le(file.data + 22) == 2 &&
					mh_read_u32le(file.data + 24) ==
						44100 &&
					mh_read_u32le(file.data + 28) ==
						176400 &&
					mh_read_u16le(file.data + 32) == 4 &&
					mh_read_u16le(file.data + 34) == 16 &&
					memcmp(file.data + 36, "data", 4) ==
						0 &&
					mh_read_u32le(file.data + 40) ==
						sizeof(samples),
				"WAV header describes lossless CD PCM");
			free(file.data);
		}

		check(mh_cd_audio_extract(source, entry.path, directory, 2,
			&error) == MH_ERROR_EXISTS,
			"refuse existing audio output");
		check(mh_cd_audio_extract(source, "missing", directory, 3,
			&error) == MH_ERROR_NOT_FOUND,
			"missing audio source fails");
		check(mh_user_data_read(data, "audio/track-03.wav", 100, &file,
			&error) == MH_ERROR_NOT_FOUND,
			"failure publishes no track");
		check_audio_range(source, data, directory, samples);
	}

	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(source);
}

int
main(void)
{
	check_sectors();
	check_cue();
	check_audio();

	return failures != 0;
}

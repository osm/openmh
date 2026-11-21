#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_cd_audio.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/optical_backend.h"

enum {
	TEST_SECTORS = 35
};

struct fake_drive {
	unsigned int	reads, closes;
	int		fail;
};

static int failures;

struct invalid_track {
	struct mh_optical_track	track;
	const char		*reason;
};

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static unsigned char
sample(size_t track, unsigned long offset)
{
	return (unsigned char)((offset + track * 17) % 251);
}

static enum mh_error_code
read_sectors(void *context, size_t track, unsigned long sector,
	unsigned int count, void *output, struct mh_error *error_out)
{
	struct fake_drive *drive;
	unsigned char *bytes;
	unsigned long stride;
	size_t i;

	drive = context;
	drive->reads++;
	check(count > 0 && count <= MH_OPTICAL_READ_SECTORS,
		"backend requests stay within batch limit");
	check(track < 2 && sector < TEST_SECTORS &&
			count <= TEST_SECTORS - sector,
		"backend never reads across tracks");

	if (drive->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected read failure");

	stride = track == 0 ? MH_OPTICAL_DATA_BYTES : MH_OPTICAL_AUDIO_BYTES;
	bytes = output;

	for (i = 0; i < count * stride; i++)
		bytes[i] = sample(track, sector * stride + (unsigned long)i);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_drive(void *context)
{
	struct fake_drive *drive;

	drive = context;
	drive->closes++;
}

static const struct mh_optical_ops ops = {read_sectors, close_drive};
static const struct mh_optical_track tracks[] = {
	{1, MH_OPTICAL_DATA, TEST_SECTORS},
	{2, MH_OPTICAL_AUDIO, TEST_SECTORS}};

static struct mh_optical_disc *
open_drive(struct fake_drive *drive)
{
	struct mh_optical_disc *disc;

	memset(drive, 0, sizeof(*drive));
	disc = NULL;
	check(mh_optical_disc_create(&ops, drive, tracks, 2, &disc, NULL) ==
			MH_ERROR_NONE,
		"open fake drive");

	return disc;
}

static void
check_read(const struct mh_resource_source *source, const char *path,
	size_t track, unsigned long offset, size_t length)
{
	unsigned char bytes[70000];
	size_t i;
	enum mh_error_code status;

	status = mh_resource_source_read_at(
		source, path, offset, bytes, length, NULL);
	check(status == MH_ERROR_NONE, "read an arbitrary track range");

	if (status != MH_ERROR_NONE)
		return;

	for (i = 0; i < length; i++) {
		if (bytes[i] != sample(track, offset + (unsigned long)i)) {
			check(0,
				"track bytes match across cache/sector boundaries");
			break;
		}
	}
}

static void
check_ranges(void)
{
	struct fake_drive drive;
	struct mh_optical_disc *disc;
	const struct mh_resource_source *source;
	unsigned long size;
	unsigned int reads;
	unsigned char byte;
	struct mh_file file;

	disc = open_drive(&drive);

	if (disc == NULL)
		return;

	source = mh_optical_disc_resources(disc);
	check_read(source, "track01", 0, 2047, 65537);
	check_read(source, "track01", 0, 17, 3);
	check_read(source, "track02", 1, 2351, 65537);
	check_read(source, "track02", 1, TEST_SECTORS * 2352UL - 7, 7);
	reads = drive.reads;
	check(mh_resource_source_size(source, "track02", &size, NULL) ==
				MH_ERROR_NONE &&
			size == TEST_SECTORS * 2352UL,
		"audio size includes every complete sector");
	check(mh_resource_source_read_at(
		source, "track02", size, &byte, 0, NULL) == MH_ERROR_NONE,
		"empty read at track end succeeds");
	check(mh_resource_source_read_at(source, "track02", size - 1, &byte, 2,
		NULL) == MH_ERROR_RANGE,
		"read cannot enter next track");
	check(mh_resource_source_read_at(source, "track01", ULONG_MAX, &byte, 1,
		NULL) == MH_ERROR_RANGE,
		"overflowing offset is refused");
	check(mh_resource_source_size(source, "track03", &size, NULL) ==
				MH_ERROR_NOT_FOUND &&
			size == TEST_SECTORS * 2352UL,
		"missing track preserves size output");
	check(mh_resource_source_size(source, "track01/extra", &size, NULL) ==
			MH_ERROR_NOT_FOUND,
		"track names must match exactly");
	memset(&file, 0, sizeof(file));
	check(mh_resource_source_read_bounded(
		source, "track01", 100, &file, NULL) == MH_ERROR_RANGE &&
			file.data == NULL,
		"bounded read fails before allocation or I/O");
	check(drive.reads == reads, "invalid ranges perform no device I/O");
	mh_optical_disc_close(disc);
	check(drive.closes == 1, "disc closes its backend exactly once");
}

static enum mh_error_code
cancel_read(void *context, unsigned int track, unsigned long sector,
	unsigned long total, struct mh_error *error_out)
{
	(void)context;

	(void)track;
	(void)sector;
	(void)total;

	return mh_error_set(error_out, MH_ERROR_FAILED, "cancelled");
}

static void
check_failures(void)
{
	struct fake_drive drive;
	struct mh_optical_disc *disc;
	const struct mh_resource_source *source;
	struct mh_file file;
	unsigned char bytes[2];
	unsigned int reads;

	disc = open_drive(&drive);

	if (disc == NULL)
		return;

	source = mh_optical_disc_resources(disc);
	drive.fail = 1;
	memset(&file, 0, sizeof(file));
	check(mh_resource_source_read(source, "track02", &file, NULL) ==
				MH_ERROR_IO &&
			file.data == NULL && file.size == 0,
		"device failure preserves whole-file output");
	drive.fail = 0;
	check_read(source, "track02", 1, 0, 2);
	reads = drive.reads;
	mh_optical_disc_set_progress(disc, cancel_read, NULL);
	check(mh_resource_source_read_at(source, "track01", 0, bytes,
		sizeof(bytes), NULL) == MH_ERROR_FAILED,
		"cancellation propagates before device read");
	check(drive.reads == reads, "cancelled request performs no device I/O");
	mh_optical_disc_set_progress(disc, NULL, NULL);
	check_read(source, "track01", 0, 0, 2);
	mh_optical_disc_close(disc);
}

static void
check_invalid_toc(void)
{
	static const struct invalid_track cases[] = {
		{{1, MH_OPTICAL_AUDIO, TEST_SECTORS},
			"reject duplicate track number"},
		{{100, MH_OPTICAL_AUDIO, TEST_SECTORS},
			"reject track number above 99"},
		{{2, (enum mh_optical_track_kind)3, TEST_SECTORS},
			"reject unknown track kind"},
		{{2, MH_OPTICAL_AUDIO, 0}, "reject empty track"},
		{{2, MH_OPTICAL_AUDIO, ULONG_MAX},
			"reject overflowing track size"}};

	struct fake_drive drive;
	struct mh_optical_disc *disc, *output;
	struct mh_optical_track invalid[2];
	size_t i;

	disc = open_drive(&drive);

	if (disc == NULL)
		return;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memcpy(invalid, tracks, sizeof(invalid));
		invalid[1] = cases[i].track;

		output = disc;
		check(mh_optical_disc_create(&ops, &drive, invalid, 2, &output,
			NULL) == MH_ERROR_INVALID &&
				output == disc,
			cases[i].reason);
	}

	check(drive.closes == 0,
		"failed create does not take backend ownership");
	check(mh_optical_disc_count(disc) == 2 &&
			mh_optical_disc_track(disc, 2) == NULL,
		"track access is bounded");
	mh_optical_disc_close(disc);
}

static void
check_audio(void)
{
	struct fake_drive drive;
	struct mh_optical_disc *disc;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_file file;
	enum mh_error_code status;
	size_t i;

	disc = open_drive(&drive);
	data = NULL;
	directory = NULL;
	status = mh_memory_user_data_open(&data, NULL);

	if (disc != NULL && status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(data, "audio", NULL);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_open_directory(
			data, "audio", "resources", &directory, NULL);

	if (status == MH_ERROR_NONE)
		status = mh_cd_audio_extract(mh_optical_disc_resources(disc),
			"track02", directory, 2, NULL);

	check(status == MH_ERROR_NONE,
		"extract a drive track through WAV writer");

	if (status == MH_ERROR_NONE) {
		status = mh_user_data_read(
			data, "audio/track-02.wav", 100000, &file, NULL);
		check(status == MH_ERROR_NONE, "read extracted audio");

		if (status == MH_ERROR_NONE) {
			check(file.size == 44 + TEST_SECTORS * 2352UL,
				"WAV preserves complete track length");

			for (i = 44; i < file.size; i++) {
				if (file.data[i] !=
					sample(1, (unsigned long)i - 44)) {
					check(0,
						"WAV preserves little-endian samples");
					break;
				}
			}

			free(file.data);
		}
	}

	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);
	mh_optical_disc_close(disc);
}

int
main(void)
{
	check_ranges();
	check_failures();
	check_invalid_toc();
	check_audio();

	return failures != 0;
}

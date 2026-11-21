#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/core/binary.h"
#include "engine/storage/demo_file.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "demo provider:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
record_take(const struct mh_user_data *data, unsigned int count,
	unsigned int frames, int conflict, enum mh_demo_version version)
{
	struct mh_demo header, loaded;
	struct mh_demo_recorder recorder;
	struct mh_demo_record_sample sample;
	struct mh_demo_record_event event;
	struct mh_demo_file *file;
	struct mh_file disk;
	struct mh_data_directory *directory;
	struct mh_data_file *identity;

	unsigned char expected[MH_DEMO_MAX_HEADER_BYTES +
		128 * MH_DEMO_MAX_FRAME_BYTES];
	size_t size;
	unsigned int i, j;

	enum mh_error_code status;

	memset(&header, 0, sizeof(header));
	header.version = version;
	header.player_count = count;
	header.laps = 1;
	strcpy(header.track, "RedRock");
	memset(&sample, 0, sizeof(sample));
	sample.active_mask = (1U << count) - 1;
	sample.unavailable_mask = 1U << (count - 1);

	for (i = 0; i < count; i++) {
		header.players[i].active = 1;
		sprintf(header.players[i].horn, "Horn%u", i);

		for (j = 0; j < 30; j++)
			sample.bodies[i][j] = (unsigned char)(i * 31 + j);
	}

	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "takes", "assets", &directory,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_open_in(directory, "take.MDE", 1, &header, &file,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "takes/take.MDE", sizeof(expected), &disk,
		NULL) == MH_ERROR_NOT_FOUND);
	size = mh_demo_header_size(version);
	CHECK(mh_demo_record_step(&recorder, 1000, &sample, &event, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= frames; i++) {
		CHECK(mh_demo_record_step(&recorder, 1000 + i * 63, &sample,
			&event, NULL) == MH_ERROR_NONE);
		CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
		memcpy(expected + size, event.data, event.size);
		size += event.size;
	}

	for (i = 0; i < count; i++)
		header.players[i].crossings[1][2] = 54321 + i;

	CHECK(mh_demo_record_finish(&recorder, &sample, header.players,
		sample.active_mask, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	memcpy(expected + size, event.data, event.size);
	size += event.size;
	CHECK(mh_demo_header_write(&recorder.header, expected,
		mh_demo_header_size(version), NULL) == MH_ERROR_NONE);

	if (conflict)
		CHECK(mh_user_data_replace(data, "takes/take.MDE",
			"preserve me", 11, NULL) == MH_ERROR_NONE);

	status = mh_demo_file_finish(
		file, &recorder.header, recorder.frame_count, NULL);

	if (conflict) {
		CHECK(status != MH_ERROR_NONE &&
			mh_user_data_read(data, "takes/take.MDE",
				sizeof(expected), &disk,
				NULL) == MH_ERROR_NONE);
		CHECK(disk.size == 11 &&
			memcmp(disk.data, "preserve me", 11) == 0);
		free(disk.data);

	} else if (recorder.frame_count < 64) {
		CHECK(status != MH_ERROR_NONE &&
			mh_user_data_read(data, "takes/take.MDE",
				sizeof(expected), &disk,
				NULL) == MH_ERROR_NOT_FOUND);
	} else {
		CHECK(status == MH_ERROR_NONE &&
			mh_user_data_read(data, "takes/take.MDE",
				sizeof(expected), &disk,
				NULL) == MH_ERROR_NONE);
		CHECK(disk.size == size &&
			memcmp(disk.data, expected, size) == 0);
		CHECK(mh_demo_read(disk.data, disk.size, &loaded, NULL) ==
			MH_ERROR_NONE);
		CHECK(loaded.frame_count == recorder.frame_count &&
			loaded.player_count == count &&
			loaded.version == version);
		CHECK(version == MH_DEMO_VERSION_25 ||
			strcmp(loaded.players[count - 1].horn,
				header.players[count - 1].horn) == 0);
		CHECK(loaded.players[count - 1].crossings[1][2] ==
			54320 + count);
		CHECK(mh_read_u32le(disk.data + (version ? 6705 : 4012)) ==
			(sample.active_mask |
				((unsigned long)sample.unavailable_mask
					<< (version ? 13 : 8))));
		CHECK(mh_read_u32le(disk.data + disk.size - (8 + 30 * count)) ==
			(sample.active_mask |
				((version || frames % 32 == 0)
						? (unsigned long)sample
								.unavailable_mask
							<< (version ? 13 : 8)
						: 0)));
		CHECK(disk.size ==
			(version ? 6705U : 4012U) +
				recorder.frame_count * (8 + 30 * count));

		free(disk.data);
	}

	if (conflict || recorder.frame_count >= 64) {
		CHECK(mh_user_data_open_directory(data, "takes", "assets",
			&directory, NULL) == MH_ERROR_NONE);
		CHECK(mh_data_directory_open_file(directory, "take.MDE",
			&identity, NULL) == MH_ERROR_NONE);
		CHECK(mh_data_file_remove(identity, NULL) == MH_ERROR_NONE);
		mh_data_file_close(identity);
		mh_data_directory_close(directory);
	}

	return 1;
}

static int
save_choices(const struct mh_user_data *data)
{
	struct mh_demo header;
	struct mh_demo_file *file, *saved_file;
	struct mh_data_directory *directory;
	struct mh_demo_record_event event;
	struct mh_file bytes;
	int exists;

	memset(&header, 0, sizeof(header));
	header.player_count = header.laps = header.players[0].active = 1;
	strcpy(header.track, "RedRock");
	CHECK(mh_user_data_open_directory(data, "takes", "assets", &directory,
		NULL) == MH_ERROR_NONE);
	file = NULL;
	CHECK(mh_demo_file_open_in(directory, "../invalid", 1, &header, &file,
		NULL) == MH_ERROR_INVALID &&
		file == NULL);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_open_in(directory, "private.MDE", 1, &header, &file,
		NULL) == MH_ERROR_NONE);
	memset(&event, 0, sizeof(event));
	event.size = MH_DEMO_BLOCK_FRAMES *
		(MH_DEMO_FRAME_HEADER_BYTES + MH_DEMO_VEHICLE_BYTES);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_seal(file, &header, 64, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_probe(file, "assets",
		(const unsigned char *)"Friday", &exists,
		NULL) == MH_ERROR_NONE &&
		!exists);
	CHECK(mh_user_data_replace(data, "takes/fRiDaY.Mde", "older", 5,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_file_probe(file, "assets",
		(const unsigned char *)"FRIDAY", &exists,
		NULL) == MH_ERROR_NONE &&
		exists);
	CHECK(mh_demo_file_save(file, 0, NULL) == MH_ERROR_EXISTS);
	CHECK(mh_user_data_replace(data, "takes/fRiDaY.Mde", "new owner", 9,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_save(file, 1, NULL) == MH_ERROR_STATE);
	CHECK(mh_user_data_read(data, "takes/fRiDaY.Mde", 9, &bytes, NULL) ==
			MH_ERROR_NONE &&
		bytes.size == 9);
	free(bytes.data);
	CHECK(mh_demo_file_probe(file, "assets",
		(const unsigned char *)"FRIDAY", &exists,
		NULL) == MH_ERROR_NONE &&
		exists);
	CHECK(mh_demo_file_save(file, 1, NULL) == MH_ERROR_NONE);
	mh_demo_file_abort(file);
	CHECK(mh_user_data_read(data, "takes/fRiDaY.Mde", 65536, &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_read(bytes.data, bytes.size, &header, NULL) ==
			MH_ERROR_NONE &&
		header.frame_count == 64);
	free(bytes.data);
	CHECK(mh_user_data_read(data, "takes/private.MDE", 65536, &bytes,
		NULL) == MH_ERROR_NOT_FOUND);

	/*
	 * An exclusive-open failure leaves both the output and directory owned
	 * here.
	 */
	CHECK(mh_user_data_open_directory(data, "takes", "assets", &directory,
		NULL) == MH_ERROR_NONE);
	file = NULL;
	CHECK(mh_demo_file_open_in(directory, "FRIDAY.MDE", 1, &header, &file,
		NULL) == MH_ERROR_EXISTS &&
		file == NULL);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_open_in(directory, "short.MDE", 1, &header, &file,
		NULL) == MH_ERROR_NONE);
	saved_file = file;
	CHECK(mh_demo_file_seal(file, &header, 0, NULL) == MH_ERROR_INVALID);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_STATE);
	CHECK(mh_demo_file_publish(file, NULL) == MH_ERROR_STATE);
	mh_demo_file_abort(saved_file);

	return 1;
}

static int
storage_round_trips(void)
{
	static const unsigned int frames[] = {0, 1, 32, 33, 63, 64, 65, 127};
	const struct mh_user_data *data, *other_data;
	struct mh_file bytes;
	unsigned int count, i, version;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "takes", NULL) ==
		MH_ERROR_NONE);

	for (version = 0; version < 2; version++)
		for (count = 1; count <= (version ? 13U : 8U); count++)
			for (i = 0; i < sizeof(frames) / sizeof(frames[0]); i++)
				CHECK(record_take(data, count, frames[i], 0,
					(enum mh_demo_version)version));

	CHECK(record_take(data, 8, 65, 1, MH_DEMO_VERSION_25));
	CHECK(record_take(data, 13, 65, 1, MH_DEMO_VERSION_30));
	CHECK(save_choices(data));
	CHECK(mh_user_data_read(other_data, "takes/fRiDaY.Mde", 65536, &bytes,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_user_data_close(other_data);
	mh_memory_user_data_close(data);

	return 1;
}

int
main(void)
{
	if (!storage_round_trips())
		return 1;

	puts("Demo providers: memory round trips, protected publication, retry, replacement, ownership and isolated views passed");

	return 0;
}

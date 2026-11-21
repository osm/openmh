#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track_flag.h"
#include "engine/storage/track_flag_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "marker provider line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
resource_formats(const unsigned char *raw, size_t raw_size)
{
	const struct mh_resource_source *source;
	struct mh_resource_entry entries[4];
	unsigned char *wrapped, *packed, *compressed;
	size_t size, compressed_size, i;
	int available;

	CHECK(mh_tbl_encode_named(raw, raw_size, NULL, &wrapped, &size, NULL) ==
		MH_ERROR_NONE);
	packed = malloc(raw_size + 4);
	CHECK(packed != NULL);

	for (i = 0; i < 4; i++)
		packed[i] = (unsigned char)((raw_size >> (8 * i)) & 255);

	memcpy(packed + 4, raw, raw_size);
	CHECK(mh_tbl_encode_named(packed, raw_size + 4, NULL, &compressed,
		&compressed_size, NULL) == MH_ERROR_NONE);
	free(packed);
	compressed[3] = 7;
	entries[0].path = "tracks/RedRock.FLG";
	entries[0].data = raw;
	entries[0].size = raw_size;
	entries[1].path = "wrapped";
	entries[1].data = wrapped;
	entries[1].size = size;
	entries[2].path = "compressed";
	entries[2].data = compressed;
	entries[2].size = compressed_size;
	entries[3].path = "broken";
	entries[3].data = (const unsigned char *)"TBL";
	entries[3].size = 3;
	CHECK(mh_memory_resources_open(entries, 4, &source, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		CHECK(mh_track_flag_resource_load_from(source, entries[i].path,
			"redROCK", &available, NULL) == MH_ERROR_NONE &&
			available == 1);
		CHECK(mh_track_flag_resource_load_from(source, entries[i].path,
			"Goldbridge", &available,
			NULL) == MH_ERROR_NONE &&
			available == 0);
	}

	available = 7;
	CHECK(mh_track_flag_resource_load_from(source, "broken", "RedRock",
		&available, NULL) != MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_track_flag_resource_load_from(source, "missing", "RedRock",
		&available, NULL) == MH_ERROR_NONE &&
		available == 0);
	mh_memory_resources_close(source);
	free(compressed);
	free(wrapped);

	return 1;
}

static int
saved_markers(const unsigned char *expected, size_t size)
{
	const struct mh_user_data *data, *other;
	struct mh_file bytes;
	int available;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_flag_file_load_from(data, "redrock.flg", "RedRock",
		&available, NULL) == MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_track_flag_file_save_to(
		data, "installed", "", "RedRock", NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "redrock.flg", size, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == size && memcmp(bytes.data, expected, size) == 0);
	free(bytes.data);
	CHECK(mh_track_flag_file_load_from(data, "redrock.flg", "RedRock",
		&available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_track_flag_file_load_from(other, "redrock.flg", "RedRock",
		&available, NULL) == MH_ERROR_NONE &&
		available == 0);

	/*
	 * Retain a unique DOS spelling and replace even a malformed old marker.
	 */
	CHECK(mh_user_data_create_directory(data, "Progress", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "Progress/redrock.fLg", "TBL", 3,
		NULL) == MH_ERROR_NONE);
	available = 7;
	CHECK(mh_track_flag_file_load_from(data, "Progress/redrock.fLg",
		"RedRock", &available, NULL) != MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_track_flag_file_save_to(data, "installed", "Progress/",
		"RedRock", NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "Progress/RedRock.FLG", size, &bytes,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_track_flag_file_load_from(data, "Progress/redrock.fLg",
		"RedRock", &available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_user_data_replace(data, "Progress/RedRock.FLG", "alias", 5,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save_to(data, "installed", "Progress",
		"RedRock", NULL) == MH_ERROR_INVALID);
	CHECK(mh_user_data_read(data, "Progress/RedRock.FLG", 5, &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 5 && memcmp(bytes.data, "alias", 5) == 0);
	free(bytes.data);
	CHECK(mh_track_flag_file_load_from(data, "Progress/redrock.fLg",
		"RedRock", &available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_user_data_create_directory(
		data, "Progress/Goldbridge.FLG", NULL) == MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save_to(data, "installed", "Progress",
		"Goldbridge", NULL) != MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save_to(data, "installed", "Missing",
		"RedRock", NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_track_flag_file_save_to(data, "installed", "", "../escape",
		NULL) == MH_ERROR_INVALID);
	mh_memory_user_data_close(other);
	mh_memory_user_data_close(data);

	return 1;
}

int
main(void)
{
	unsigned char *raw;
	size_t size;
	int success;

	if (mh_track_flag_encode("RedRock", &raw, &size, NULL) != MH_ERROR_NONE)
		return 1;

	success = resource_formats(raw, size) && saved_markers(raw, size);
	free(raw);

	if (success)
		puts("Track markers: memory resources/user data, wrappers and case-preserving replacement passed");

	return success ? 0 : 1;
}

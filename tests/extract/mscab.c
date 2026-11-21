#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/mscab_provider.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

enum {
	FIXTURE_MEMBER_OFFSET_FIELD = 48
};

/*
 * Independent Python zlib fixture: block two requires block one's dictionary.
 */
static const unsigned char fixture[] = {0x4d, 0x53, 0x43, 0x46, 0x00, 0x00,
	0x00, 0x00, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x64, 0x69, 0x73, 0x6b, 0x31,
	0x5c, 0x64, 0x61, 0x74, 0x61, 0x31, 0x2e, 0x63, 0x61, 0x62, 0x00, 0x6f,
	0xd5, 0x18, 0x50, 0x36, 0x00, 0x32, 0x00, 0x43, 0x4b, 0x4b, 0x4e, 0x4c,
	0xca, 0xcc, 0x4b, 0x2d, 0x51, 0x48, 0xca, 0xc9, 0x4f, 0xce, 0x56, 0xc8,
	0xc8, 0x2c, 0x2e, 0xc9, 0x2f, 0xaa, 0xb4, 0x52, 0x48, 0x4c, 0x4a, 0x4e,
	0x49, 0x4d, 0x4b, 0xcf, 0xc8, 0xcc, 0xca, 0xce, 0xc9, 0xcd, 0xcb, 0x2f,
	0x28, 0x2c, 0x2a, 0x2e, 0x29, 0x2d, 0x2b, 0xaf, 0xa8, 0xac, 0x52, 0x04,
	0x00, 0x44, 0x4c, 0x20, 0x26, 0x07, 0x00, 0x32, 0x00, 0x43, 0x4b, 0x4b,
	0x26, 0x59, 0x07, 0x00};
static int failures;

static void
check(int success, const char *message)
{
	if (!success) {
		fprintf(stderr, "%s\n", message);
		failures++;
	}
}

static void
reject(const unsigned char *bytes, size_t size)
{
	struct mh_archive *archive;
	struct mh_error error;

	archive = NULL;
	check(mh_mscab_open(bytes, size, &archive, &error) != MH_ERROR_NONE &&
			archive == NULL,
		"invalid cabinet rejected without publishing output");
	mh_archive_close(archive);
}

int
main(void)
{
	static const char expected[] =
		"cabinet block history: abcdefghijklmnopqrstuvwxyz!"
		"cabinet block history: abcdefghijklmnopqrstuvwxyz!";
	static const unsigned int corrupt[] = {0, 8, 16, 24, 26, 28, 30, 36, 40,
		42, 44, FIXTURE_MEMBER_OFFSET_FIELD, 52, 77, 83, 85};

	struct mh_archive *archive;
	struct mh_file file;
	struct mh_error error;
	unsigned char bytes[sizeof(fixture)];
	size_t i;

	archive = NULL;
	check(mh_mscab_open(fixture, sizeof(fixture), &archive, &error) ==
			MH_ERROR_NONE,
		"open two-block MSZIP cabinet");

	if (archive != NULL) {
		check(mh_archive_count(archive) == 1 &&
				strcmp(mh_archive_entry(archive, 0)->path,
					"disk1/data1.cab") == 0,
			"cabinet-root name normalized to a safe relative path");
		check(mh_archive_read(archive, 0, sizeof(expected), &file,
			&error) == MH_ERROR_NONE,
			"read member crossing MSZIP blocks");

		if (failures == 0) {
			check(file.size == sizeof(expected) - 1 &&
					memcmp(file.data, expected,
						file.size) == 0,
				"dictionary history preserved");
			free(file.data);
		}

		mh_archive_close(archive);
	}

	for (i = 0; i < sizeof(fixture); i++)
		reject(fixture, i);

	for (i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
		memcpy(bytes, fixture, sizeof(bytes));
		bytes[corrupt[i]] ^= 1;
		reject(bytes, sizeof(bytes));
	}

	memcpy(bytes, fixture, sizeof(bytes));
	mh_write_u32le(bytes + FIXTURE_MEMBER_OFFSET_FIELD, MH_U32_MAX);
	archive = NULL;
	check(mh_mscab_open(bytes, sizeof(bytes), &archive, &error) ==
				MH_ERROR_RANGE &&
			archive == NULL,
		"reject out-of-range member offset before adding its length");
	mh_archive_close(archive);

	memcpy(bytes, fixture, sizeof(bytes));
	bytes[60] = '/';
	reject(bytes, sizeof(bytes));

	return failures != 0;
}

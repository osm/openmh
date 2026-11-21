#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "ZIP check line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_reads(const struct mh_resource_source *zip, const char *path,
	const char *other, const struct mh_file *expected)
{
	unsigned char bytes[65536];
	struct mh_file file;
	unsigned long length;
	size_t offset, count;

	CHECK(mh_resource_source_size(zip, path, &length, NULL) ==
		MH_ERROR_NONE);
	CHECK(length == expected->size);

	/* Reverse reads exercise seeking and the final short chunk. */
	offset = expected->size;

	while (offset != 0) {
		count = offset > sizeof(bytes) ? sizeof(bytes) : offset;
		offset -= count;
		CHECK(mh_resource_source_read_at(zip, path,
			(unsigned long)offset, bytes, count,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(bytes, expected->data + offset, count) == 0);
	}

	/*
	 * Switching compressed entries evicts the previous cached allocation.
	 */
	CHECK(mh_resource_source_read_at(zip, other, 0, bytes, 5, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(bytes, "other", 5) == 0);

	CHECK(mh_resource_source_read_at(zip, path, length, bytes, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_read_at(zip, path, length, bytes, 1, NULL) ==
		MH_ERROR_RANGE);
	file.data = NULL;
	file.size = 17;

	if (expected->size != 0) {
		CHECK(mh_resource_source_read_bounded(zip, path,
			expected->size - 1, &file,
			NULL) == MH_ERROR_RANGE);
		CHECK(file.data == NULL && file.size == 17);
	}

	CHECK(mh_resource_source_read(zip, path, &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == expected->size &&
		memcmp(file.data, expected->data, file.size) == 0);
	free(file.data);

	/*
	 * A whole read transfers ownership; the next range must remain usable.
	 */
	count = expected->size > sizeof(bytes) ? sizeof(bytes) : expected->size;
	CHECK(mh_resource_source_read_at(zip, path, 0, bytes, count, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(bytes, expected->data, count) == 0);

	return 0;
}

static int
check_rejection(const struct mh_resource_source *zip, const char *path,
	enum mh_error_code expected)
{
	struct mh_file file;
	unsigned char byte;

	file.data = NULL;
	file.size = 17;
	CHECK(mh_resource_source_read(zip, path, &file, NULL) == expected);
	CHECK(file.data == NULL && file.size == 17);
	CHECK(mh_resource_source_read_at(zip, path, 0, &byte, 0, NULL) ==
		expected);

	return 0;
}

int
main(int argc, char *argv[])
{
	const struct mh_resource_source *zip;
	struct mh_file expected;
	struct mh_error error;
	char path[4096], other[4096];
	int failed;
	enum mh_error_code status;

	if (argc != 5 || strlen(argv[2]) + strlen(argv[3]) + 2 > sizeof(path) ||
		strlen(argv[2]) + sizeof("/Other") > sizeof(other))
		return 1;

	zip = NULL;
	status = mh_zip_resource_provider()->open(
		mh_filesystem_resources(), argv[2], &zip, &error);

	if (strcmp(argv[1], "index-error") == 0)
		return status == MH_ERROR_NONE || zip != NULL;

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	sprintf(path, "%s/%s", argv[2], argv[3]);
	sprintf(other, "%s/Other", argv[2]);

	if (strcmp(argv[1], "read") == 0) {
		CHECK(mh_resource_source_read(mh_filesystem_resources(),
			argv[4], &expected, NULL) == MH_ERROR_NONE);
		failed = check_reads(zip, path, other, &expected);
		free(expected.data);
	} else {
		failed = check_rejection(zip, path,
			strcmp(argv[1], "limit") == 0 ? MH_ERROR_RANGE
						: MH_ERROR_INVALID);
	}

	mh_zip_resource_provider()->close(zip);

	return failed;
}

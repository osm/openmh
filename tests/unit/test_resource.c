#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "engine/core/error.h"
#include "interfaces/storage/resource_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "resource line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_source(const struct mh_resource_source *source, int argc, char *argv[])
{
	struct mh_file file;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long size;
	unsigned char retained;
	char range[4];

	CHECK(argc == 4);
	CHECK(mh_resource_source_read_at(source, argv[1], 2, range, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(range, "xtur", 4) == 0);
	CHECK(mh_resource_source_read_at(source, argv[1], 7, range, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_read_at(source, argv[1], 6, range, 2, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_resource_source_read_at(source, argv[3], 0, range, 1, NULL) ==
		MH_ERROR_NOT_FOUND);

	CHECK(mh_resource_source_read(source, argv[1], &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size == 7 && memcmp(file.data, "fixture", 7) == 0);
	free(file.data);
	CHECK(mh_resource_source_size(source, argv[1], &size, NULL) ==
			MH_ERROR_NONE &&
		size == 7);
	CHECK(mh_resource_source_read(source, argv[2], &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size == 0 && file.data != NULL);
	free(file.data);
	CHECK(mh_resource_source_size(source, argv[2], &size, NULL) ==
			MH_ERROR_NONE &&
		size == 0);

	CHECK(mh_resource_source_read_bounded(
		source, argv[1], 7, &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 7 && memcmp(file.data, "fixture", 7) == 0);
	free(file.data);
	CHECK(mh_resource_source_read_bounded(
		source, argv[2], 0, &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 0 && file.data != NULL);
	free(file.data);
	file.data = &retained;
	file.size = 17;
	size = 23;
	CHECK(mh_resource_source_read_bounded(
		source, argv[1], 6, &file, NULL) == MH_ERROR_RANGE);
	CHECK(file.data == &retained && file.size == 17);
	CHECK(mh_resource_source_read(source, argv[3], &file, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(file.data == &retained && file.size == 17);
	CHECK(mh_resource_source_size(source, argv[3], &size, NULL) ==
			MH_ERROR_NOT_FOUND &&
		size == 23);
	CHECK(mh_resource_source_read(source, NULL, &file, NULL) ==
		MH_ERROR_INVALID);
	CHECK(file.data == &retained && file.size == 17);
	CHECK(mh_resource_source_read(source, argv[1], NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_resource_source_size(source, NULL, &size, NULL) ==
			MH_ERROR_INVALID &&
		size == 23);
	CHECK(mh_resource_source_size(source, argv[1], NULL, NULL) ==
		MH_ERROR_INVALID);
	status = mh_resource_source_read(source, argv[3], &file, &error);
	CHECK(status == MH_ERROR_NOT_FOUND && error.code == status);
	CHECK(strcmp(error.message, "cannot open resource") == 0);
	CHECK(file.data == &retained && file.size == 17);
	status = mh_resource_source_size(source, argv[1], &size, &error);
	CHECK(status == MH_ERROR_NONE && error.code == status &&
		error.message == NULL && size == 7);

	puts("Resource reads, empty resources and failure atomicity passed");

	return 0;
}

int
main(int argc, char *argv[])
{
	return check_source(mh_filesystem_resources(), argc, argv);
}

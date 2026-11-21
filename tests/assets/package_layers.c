#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "engine/assets/packages.h"
#include "engine/core/text.h"
#include "engine/storage/digest.h"

struct comparison {
	struct mh_vfs	*actual, *expected;
	char		relative[4096];
	unsigned int	count;
	int		failed;
};

static enum mh_directory_action
compare_child(void *context, const char *name)
{
	struct comparison *comparison;
	char actual[8192], expected[8192], a[MH_SHA256_TEXT_SIZE],
		b[MH_SHA256_TEXT_SIZE];
	unsigned long size_a, size_b;
	size_t length;
	enum mh_error_code status;

	comparison = context;
	length = strlen(comparison->relative);

	if (length == 0 && mh_ascii_equal(name, "patch.txt"))
		return MH_DIRECTORY_CONTINUE;

	if (length + strlen(name) + 2 >= sizeof(comparison->relative))
		return MH_DIRECTORY_FAIL;

	if (length != 0)
		strcat(comparison->relative, "/");

	strcat(comparison->relative, name);
	sprintf(actual, "%s/%s", mh_vfs_root(comparison->actual),
		comparison->relative);
	sprintf(expected, "%s/%s", mh_vfs_root(comparison->expected),
		comparison->relative);
	status = mh_resource_source_size(mh_vfs_resources(comparison->expected),
		expected, &size_b, NULL);

	if (status == MH_ERROR_NOT_FOUND)
		status = mh_resource_source_list(
			mh_vfs_resources(comparison->expected), expected,
			compare_child, comparison, NULL);
	else if (status == MH_ERROR_NONE) {
		status = mh_resource_sha256(
			mh_vfs_resources(comparison->expected), expected, b,
			&size_b, NULL);

		if (status == MH_ERROR_NONE)
			status = mh_resource_sha256(
				mh_vfs_resources(comparison->actual), actual, a,
				&size_a, NULL);

		if (status == MH_ERROR_NONE &&
			(size_a != size_b || strcmp(a, b) != 0))
			status = MH_ERROR_INVALID;

		comparison->count++;
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "resource mismatch: %s\n",
			comparison->relative);
		comparison->failed = 1;
	}

	comparison->relative[length] = 0;

	return status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
				: MH_DIRECTORY_FAIL;
}

int
main(int argc, char *argv[])
{
	struct comparison comparison;
	struct mh_content_info info;
	struct mh_error error;
	enum mh_error_code status;

	memset(&comparison, 0, sizeof(comparison));

	if (argc != 2 && argc != 4)
		return 1;

	status = mh_content_mount(mh_filesystem_resources(),
		mh_zip_resource_provider(), argv[1], &comparison.actual,
		&error);

	if (status == MH_ERROR_NONE)
		status = mh_content_probe(mh_vfs_resources(comparison.actual),
			mh_vfs_root(comparison.actual), NULL, &info, &error);

	if (status == MH_ERROR_NONE && argc == 4 &&
		(int)info.revision != atoi(argv[3]))
		status = mh_error_set(
			&error, MH_ERROR_INVALID, "wrong content revision");

	if (status == MH_ERROR_NONE && argc == 4)
		status = mh_vfs_open(mh_filesystem_resources(),
			mh_zip_resource_provider(), argv[2],
			&comparison.expected, &error);

	if (status == MH_ERROR_NONE && argc == 4)
		status = mh_resource_source_list(
			mh_vfs_resources(comparison.expected),
			mh_vfs_root(comparison.expected), compare_child,
			&comparison, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);
	else if (argc == 4)
		printf("%u combined resources match reference\n",
			comparison.count);
	else
		printf("%s: revision %d\n", info.edition->id,
			(int)info.revision);
	mh_vfs_close(comparison.actual);
	mh_vfs_close(comparison.expected);

	return status != MH_ERROR_NONE || comparison.failed;
}

#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/zip_provider.h"

/* Bound traversal and expansion per input, including compressed range reads. */
enum {
	FUZZ_VISIT_LIMIT = 32,
	FUZZ_PATH_BYTES = 2048,
	FUZZ_MEMBER_BYTES = 64 * 1024
};

struct package_walk {
	const struct mh_resource_source	*zip;
	char				path[FUZZ_PATH_BYTES];
	size_t				remaining;
};

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

static void
check_member(const struct mh_resource_source *zip, const char *path,
	unsigned long size)
{
	struct mh_file file;
	unsigned char last;
	enum mh_error_code status, range_status;

	if (mh_resource_source_read_at(zip, path, size, &last, 1, NULL) !=
		MH_ERROR_RANGE)
		abort();

	file.data = NULL;
	file.size = 0;
	range_status = MH_ERROR_RANGE;

	if (size <= FUZZ_MEMBER_BYTES)
		range_status = mh_resource_source_read_at(zip, path,
			size == 0 ? 0 : size - 1, &last, size != 0, NULL);

	status = mh_resource_source_read_bounded(
		zip, path, FUZZ_MEMBER_BYTES, &file, NULL);

	if (size > FUZZ_MEMBER_BYTES && status != MH_ERROR_RANGE)
		abort();

	if (status == MH_ERROR_NONE) {
		if (range_status != MH_ERROR_NONE || file.size != size ||
			file.data == NULL ||
			(size != 0 && file.data[file.size - 1] != last))
			abort();

		free(file.data);
	} else if (file.data != NULL || file.size != 0)
		abort();
}

static enum mh_directory_action
visit_member(void *context, const char *name)
{
	struct package_walk *walk;
	size_t parent, length;
	unsigned long size;
	enum mh_error_code status;

	walk = context;

	if (walk->remaining == 0)
		return MH_DIRECTORY_STOP;

	walk->remaining--;
	length = strlen(name);

	if (length == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
		strpbrk(name, "/\\:") != NULL)
		abort();

	parent = strlen(walk->path);

	if (length + 2 > sizeof(walk->path) - parent)
		return MH_DIRECTORY_CONTINUE;

	walk->path[parent] = '/';
	memcpy(walk->path + parent + 1, name, length + 1);
	status = mh_resource_source_size(walk->zip, walk->path, &size, NULL);

	if (status == MH_ERROR_NONE)
		check_member(walk->zip, walk->path, size);
	else if (status != MH_ERROR_NOT_FOUND ||
		mh_resource_source_list(walk->zip, walk->path, visit_member,
			walk, NULL) != MH_ERROR_NONE)
		abort();

	walk->path[parent] = 0;

	return MH_DIRECTORY_CONTINUE;
}

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static const char path[] = "test.ass";
	const struct mh_resource_archive_provider *provider;
	const struct mh_resource_source *source;
	struct mh_resource_entry entry;
	struct package_walk walk;

	entry.path = path;
	entry.data = data;
	entry.size = size;

	if (mh_memory_resources_open(&entry, 1, &source, NULL) != MH_ERROR_NONE)
		return 0;

	provider = mh_zip_resource_provider();
	walk.zip = NULL;

	if (provider->open(source, path, &walk.zip, NULL) == MH_ERROR_NONE) {
		strcpy(walk.path, path);
		walk.remaining = FUZZ_VISIT_LIMIT;

		if (mh_resource_source_list(walk.zip, walk.path, visit_member,
			&walk, NULL) != MH_ERROR_NONE)
			abort();

		provider->close(walk.zip);
	} else if (walk.zip != NULL)
		abort();

	mh_memory_resources_close(source);

	return 0;
}

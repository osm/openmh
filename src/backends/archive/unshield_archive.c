#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_internal.h"
#include "backends/archive/unshield_provider.h"
#include "backends/archive/unshield_zip.h"
#include "engine/archive/path.h"
#include "engine/core/text.h"

static size_t
member_count(const void *context)
{
	const struct cabinet *cabinet;

	cabinet = context;

	return cabinet->count;
}

static const struct mh_archive_entry *
member_entry(const void *context, size_t index)
{
	const struct cabinet *cabinet;

	cabinet = context;

	return &cabinet->members[index].entry;
}

static enum mh_error_code
read_member(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	struct cabinet *cabinet;
	struct cabinet_member *member;
	struct mh_file file;
	int success;

	cabinet = context;
	member = &cabinet->members[index];
	file.size = member->entry.size;
	file.data = malloc(file.size == 0 ? 1 : file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding cabinet member");

	cabinet->output = file.data;
	cabinet->output_size = file.size;
	cabinet->written = 0;
	success = unshield_file_save(cabinet->reader, member->index, "member");
	cabinet->output = NULL;

	if (!success || cabinet->written != file.size) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cabinet member decode or size check failed");
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_archive(void *context)
{
	struct cabinet *cabinet;

	cabinet = context;

	if (cabinet->reader != NULL)
		unshield_close(cabinet->reader);

	free(cabinet->members);
	free(cabinet);
}

static enum mh_error_code
add_member(struct cabinet *cabinet, int index, int components,
	struct mh_error *error_out)
{
	struct cabinet_member *member;
	const char *directory, *name, *group_name;
	UnshieldFileGroup *group;
	char grouped[MH_ARCHIVE_PATH_SIZE];
	int g;
	int directory_index;
	size_t i;
	enum mh_error_code status;

	directory_index = unshield_file_directory(cabinet->reader, index);

	if (directory_index < 0 ||
		directory_index >= unshield_directory_count(cabinet->reader))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cabinet member directory is invalid");

	directory = unshield_directory_name(cabinet->reader, directory_index);
	name = unshield_file_name(cabinet->reader, index);

	if (components) {
		group_name = "Ungrouped";

		for (g = 0; g < unshield_file_group_count(cabinet->reader);
			g++) {
			group = unshield_file_group_get(cabinet->reader, g);

			if (group != NULL &&
				(unsigned int)index >= group->first_file &&
				(unsigned int)index <= group->last_file) {
				group_name = group->name;
				break;
			}
		}

		if (strlen(group_name) + strlen(directory) + 2 >
			sizeof(grouped))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"installer component path is too long");

		strcpy(grouped, group_name);

		if (*directory != 0) {
			strcat(grouped, "/");
			strcat(grouped, directory);
		}

		directory = grouped;
	}

	member = &cabinet->members[cabinet->count];
	status =
		mh_archive_path(directory, name, member->entry.path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < cabinet->count; i++) {
		if (mh_ascii_equal(
			member->entry.path, cabinet->members[i].entry.path))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate cabinet member path");
	}

	member->index = index;
	member->entry.size = unshield_file_size(cabinet->reader, index);
	cabinet->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_members(
	struct cabinet *cabinet, int components, struct mh_error *error_out)
{
	int count, i;
	enum mh_error_code status;

	count = unshield_file_count(cabinet->reader);

	if (count < 1 || count > MH_ARCHIVE_MEMBER_LIMIT ||
		(size_t)count > (size_t)-1 / sizeof(*cabinet->members))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"cabinet member count is invalid");

	cabinet->members = calloc((size_t)count, sizeof(*cabinet->members));

	if (cabinet->members == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading cabinet directory");

	for (i = 0; i < count; i++) {
		/* Deleted descriptors must not be queried for names. */
		if (!unshield_file_is_valid(cabinet->reader, i))
			continue;

		status = add_member(cabinet, i, components, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_cabinet(const void *bytes, size_t size, int components,
	struct mh_archive **output, struct mh_error *error_out)
{
	static const struct mh_archive_ops ops = {
		member_count, member_entry, read_member, close_archive};

	struct cabinet *cabinet;
	enum mh_error_code status;

	if (bytes == NULL || output == NULL || size < 4 || size > LONG_MAX ||
		memcmp(bytes, "ISc(", 4) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected a supported InstallShield cabinet");

	cabinet = calloc(1, sizeof(*cabinet));

	if (cabinet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening cabinet");

	cabinet->bytes = bytes;
	cabinet->size = size;
	unshield_set_log_level(UNSHIELD_LOG_LEVEL_ERROR);
	cabinet->reader = unshield_open2(
		MH_UNSHIELD_CABINET_NAME, &mh_unshield_memory_io, cabinet);

	if (cabinet->reader == NULL) {
		close_archive(cabinet);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot open InstallShield cabinet");
	}

	status = read_members(cabinet, components, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_archive_create(&ops, cabinet, output, error_out);

	if (status != MH_ERROR_NONE)
		close_archive(cabinet);

	return status;
}

enum mh_error_code
mh_unshield_open(const void *bytes, size_t size, struct mh_archive **output,
	struct mh_error *error_out)
{
	return open_cabinet(bytes, size, 0, output, error_out);
}

enum mh_error_code
mh_unshield_open_components(const void *bytes, size_t size,
	struct mh_archive **output, struct mh_error *error_out)
{
	return open_cabinet(bytes, size, 1, output, error_out);
}

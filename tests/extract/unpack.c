#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_unpack.h"
#include "interfaces/archive/archive_backend.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static size_t
count_entries(const void *context)
{
	(void)context;

	return 2;
}

static const struct mh_archive_entry *
entry_info(const void *context, size_t index)
{
	static const struct mh_archive_entry entries[] = {
		{"Data/nested/first", 3}, {"second", 3}};

	(void)context;

	return &entries[index];
}

static enum mh_error_code
read_entry(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file file;

	(void)context;
	file.data = malloc(3);
	file.size = 3;

	if (file.data == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "fixture allocation");

	memcpy(file.data, index == 0 ? "one" : "two", 3);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_archive(void *context)
{
	(void)context;
}

static enum mh_error_code
cancel_after_first(void *context, const char *path, size_t completed,
	size_t total, struct mh_error *error_out)
{
	(void)context;

	(void)path;
	(void)total;

	if (completed == 1)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "fixture cancellation");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
check_unpack(int cancel)
{
	static const struct mh_archive_ops ops = {
		count_entries, entry_info, read_entry, close_archive};

	struct mh_archive *archive;
	const struct mh_user_data *data;
	struct mh_error error;
	struct mh_file file;
	enum mh_error_code status;

	archive = NULL;
	data = NULL;
	status = mh_archive_create(&ops, NULL, &archive, &error);

	if (status == MH_ERROR_NONE)
		status = mh_memory_user_data_open(&data, &error);

	check(status == MH_ERROR_NONE, "create in-memory extraction providers");

	if (status == MH_ERROR_NONE) {
		status = mh_archive_unpack(archive, data, "stage/game",
			cancel ? cancel_after_first : NULL, NULL, &error);
		check(status == (cancel ? MH_ERROR_FAILED : MH_ERROR_NONE),
			"portable extraction completion/cancellation");
		status = mh_user_data_read(data, "stage/game/Data/nested/first",
			16, &file, &error);
		check(status == MH_ERROR_NONE,
			"nested member written through storage");

		if (status == MH_ERROR_NONE) {
			check(file.size == 3 &&
					memcmp(file.data, "one", 3) == 0,
				"nested bytes match");
			free(file.data);
		}

		status = mh_user_data_read(
			data, "stage/game/second", 16, &file, &error);
		check(status == (cancel ? MH_ERROR_NOT_FOUND : MH_ERROR_NONE),
			"cancelled members are never written");

		if (status == MH_ERROR_NONE) {
			check(file.size == 3 &&
					memcmp(file.data, "two", 3) == 0,
				"root bytes match");
			free(file.data);
		}
	}

	mh_archive_close(archive);
	mh_memory_user_data_close(data);
}

int
main(void)
{
	check_unpack(0);
	check_unpack(1);

	return failures != 0;
}

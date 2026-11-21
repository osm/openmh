#ifdef MH_HAVE_USER_DATA_FILES
#define _XOPEN_SOURCE 700
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <signal.h>
#include <unistd.h>

#include "../support/temporary.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_user_data.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/data_stream.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "data file:%d: %s\n", __LINE__, #x);   \
			return 0;                                              \
		}                                                              \
	} while (0)

#define FILE_NAME "Profile.CLO"

struct publication {
	const char	*previous;
	unsigned int	calls;
	int		reject;
};

static enum mh_error_code
check_publication(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct publication *publication;
	struct mh_data_file *file;
	struct mh_file bytes;
	enum mh_error_code status;
	int matches;

	publication = context;
	publication->calls++;
	status = mh_data_directory_read(
		directory, FILE_NAME, 64, &bytes, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	matches = bytes.size == strlen(publication->previous) &&
		memcmp(bytes.data, publication->previous, bytes.size) == 0;
	free(bytes.data);
	mh_data_file_close(file);

	if (!matches)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"file was published before its policy check");

	if (publication->reject)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"injected publication refusal");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#if defined(MH_HAVE_USER_DATA_FILES) && !defined(__EMSCRIPTEN__)
/* Short-write injection needs fork/setrlimit, absent from browser MEMFS. */
static enum mh_directory_action
check_temporary(void *context, const char *name)
{
	unsigned int *count;

	count = context;
	(*count)++;

	return strcmp(name, FILE_NAME) == 0 ? MH_DIRECTORY_CONTINUE
					: MH_DIRECTORY_FAIL;
}

static int
check_short_write(
	struct mh_data_directory *directory, struct mh_data_file *file)
{
	struct rlimit limit;
	char bytes[128];
	pid_t child;
	int child_status;
	unsigned int count;

	memset(bytes, 'x', sizeof(bytes));
	child = fork();
	CHECK(child >= 0);

	if (child == 0) {
		signal(SIGXFSZ, SIG_IGN);
		limit.rlim_cur = limit.rlim_max = 3;

		if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
			_exit(2);

		_exit(mh_data_file_replace(file, bytes, sizeof(bytes), NULL,
			NULL, NULL) == MH_ERROR_NONE
				? 3
				: 0);
	}

	CHECK(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	count = 0;
	CHECK(mh_data_directory_list(directory, check_temporary, &count,
		NULL) == MH_ERROR_NONE &&
		count == 1);

	return 1;
}

static int
check_short_stream(struct mh_data_directory *directory,
	struct mh_data_file *file, size_t size)
{
	struct mh_data_stream *stream;
	struct rlimit limit;
	char bytes[8192];
	pid_t child;
	enum mh_error_code status;
	int child_status, rejected;
	unsigned int count;

	memset(bytes, 'x', sizeof(bytes));
	child = fork();
	CHECK(child >= 0);

	if (child == 0) {
		signal(SIGXFSZ, SIG_IGN);
		limit.rlim_cur = limit.rlim_max = 3;

		if (setrlimit(RLIMIT_FSIZE, &limit) != 0 ||
			mh_data_stream_open(directory, &stream, NULL) !=
				MH_ERROR_NONE)
			_exit(2);

		status = mh_data_stream_append(stream, bytes, size, NULL);

		if (status == MH_ERROR_NONE)
			status = mh_data_stream_seal(stream, NULL);

		rejected = status != MH_ERROR_NONE &&
			mh_data_stream_append(stream, "x", 1, NULL) ==
				MH_ERROR_STATE &&
			mh_data_stream_seal(stream, NULL) == MH_ERROR_STATE &&
			mh_data_stream_replace(stream, file, NULL, NULL,
				NULL) == MH_ERROR_STATE;
		mh_data_stream_close(stream);
		_exit(rejected ? 0 : 3);
	}

	CHECK(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	count = 0;
	CHECK(mh_data_directory_list(directory, check_temporary, &count,
		NULL) == MH_ERROR_NONE &&
		count == 1);

	return 1;
}

#endif

static int
file_updates(
	const struct mh_user_data *data, const char *path, const char *assets)
{
	struct mh_data_directory *directory;
	struct mh_data_file *file, *observer, *fresh;
	struct mh_file bytes;
	struct publication publication;
	char key[512];

	CHECK(strlen(path) + sizeof("/" FILE_NAME) <= sizeof(key));
	sprintf(key, "%s/%s", path, FILE_NAME);
	CHECK(mh_user_data_open_directory(
		data, path, assets, &directory, NULL) == MH_ERROR_NONE);
	observer = NULL;
	CHECK(mh_data_directory_open_file(directory, FILE_NAME, &observer,
		NULL) == MH_ERROR_NOT_FOUND &&
		observer == NULL);
	CHECK(mh_data_directory_create(directory, FILE_NAME, "original", 8,
		NULL, NULL, &file, NULL) == MH_ERROR_NONE);

	/* Identity capture needs no permission to read the old payload. */
#ifdef MH_HAVE_USER_DATA_FILES
	CHECK(chmod(key, 0000) == 0);
#endif
	CHECK(mh_data_directory_open_file(
		directory, FILE_NAME, &observer, NULL) == MH_ERROR_NONE);
#ifdef MH_HAVE_USER_DATA_FILES
	CHECK(chmod(key, 0600) == 0);
#endif
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(observer, NULL) == MH_ERROR_NONE);
	publication.previous = "original";
	publication.calls = 0;
	publication.reject = 1;
	CHECK(mh_data_file_replace(file, "refused", 7, check_publication,
		&publication, NULL) == MH_ERROR_RANGE);
	CHECK(publication.calls == 1);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(observer, NULL) == MH_ERROR_NONE);
	publication.reject = 0;
	CHECK(mh_data_file_replace(file, "updated", 7, check_publication,
		&publication, NULL) == MH_ERROR_NONE);
	CHECK(publication.calls == 2);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(observer, NULL) == MH_ERROR_STATE);
	CHECK(mh_data_file_replace(observer, "stale", 5, check_publication,
		&publication, NULL) == MH_ERROR_STATE);
	CHECK(publication.calls == 2);
	CHECK(mh_data_file_remove(observer, NULL) == MH_ERROR_STATE);
	mh_data_file_close(observer);
#if defined(MH_HAVE_USER_DATA_FILES) && !defined(__EMSCRIPTEN__)
	CHECK(check_short_write(directory, file));
	CHECK(check_short_stream(directory, file, 128));
	CHECK(check_short_stream(directory, file, 8192));
#endif
	publication.previous = "updated";
	CHECK(mh_data_file_replace(file, NULL, 0, check_publication,
		&publication, NULL) == MH_ERROR_NONE);
	CHECK(publication.calls == 3);
	CHECK(mh_data_directory_read(directory, FILE_NAME, 0, &bytes, &fresh,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.data != NULL && bytes.size == 0);
	free(bytes.data);
	CHECK(mh_data_file_replace(file, NULL, 1, NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, key, "external", 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_STATE);
	CHECK(mh_data_file_replace(file, "stale", 5, NULL, NULL, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_data_file_validate(fresh, NULL) == MH_ERROR_STATE);
	mh_data_file_close(file);
	mh_data_file_close(fresh);
	CHECK(mh_data_directory_read(directory, FILE_NAME, 8, &bytes, &file,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 8 && memcmp(bytes.data, "external", 8) == 0);
	free(bytes.data);
	CHECK(mh_data_file_replace(file, "reloaded", 8, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_STATE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(mh_data_file_validate(NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_data_file_replace(NULL, "x", 1, NULL, NULL, NULL) ==
		MH_ERROR_INVALID);

	return 1;
}

static int
stream_updates(
	const struct mh_user_data *data, const char *path, const char *assets)
{
	struct mh_data_directory *directory, *other;
	struct mh_data_stream *stream;
	struct mh_data_file *file, *observer, *created;
	struct mh_file bytes;
	struct publication publication;
	unsigned char block[1024];
	unsigned int i;
	size_t position;

	CHECK(mh_user_data_open_directory(
		data, path, assets, &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, path, assets, &other, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, FILE_NAME, "original", 8,
		NULL, NULL, &file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, NULL, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_write_at(stream, 0, NULL, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "head", 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "payload", 7, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_size(stream) == 11);
	CHECK(mh_data_stream_append(stream, "x", (size_t)-1, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_data_stream_write_at(stream, 11, "!", 1, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_data_stream_append(stream, NULL, 1, NULL) == MH_ERROR_INVALID);
	CHECK(mh_data_stream_write_at(stream, 0, "DONE", 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "!", 1, NULL) == MH_ERROR_NONE);
	created = NULL;
	CHECK(mh_data_stream_create(stream, "take.MDE", NULL, NULL, &created,
		NULL) == MH_ERROR_STATE &&
		created == NULL);
	CHECK(mh_data_directory_open_file(directory, "take.MDE", &created,
		NULL) == MH_ERROR_NOT_FOUND &&
		created == NULL);
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "x", 1, NULL) == MH_ERROR_STATE);
	CHECK(mh_data_stream_write_at(stream, 0, "x", 1, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_data_stream_create(stream, FILE_NAME, NULL, NULL, &created,
		NULL) == MH_ERROR_EXISTS &&
		created == NULL);
	CHECK(mh_data_stream_create(stream, "take.MDE", NULL, NULL, &created,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_create(stream, "again.MDE", NULL, NULL, &observer,
		NULL) == MH_ERROR_STATE);
	mh_data_stream_close(stream);
	CHECK(mh_data_directory_read(directory, "take.MDE", 12, &bytes,
		&observer, NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 12 && memcmp(bytes.data, "DONEpayload!", 12) == 0);
	free(bytes.data);
	CHECK(mh_data_file_remove(created, NULL) == MH_ERROR_NONE);
	mh_data_file_close(observer);
	mh_data_file_close(created);

	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "updated", 7, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_open_file(other, FILE_NAME, &observer, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_replace(stream, observer, NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	mh_data_file_close(observer);
	CHECK(mh_data_directory_open_file(
		directory, FILE_NAME, &observer, NULL) == MH_ERROR_NONE);
	publication.previous = "original";
	publication.calls = 0;
	publication.reject = 1;
	CHECK(mh_data_stream_replace(stream, file, check_publication,
		&publication, NULL) == MH_ERROR_RANGE);
	CHECK(publication.calls == 1 &&
		mh_data_file_validate(observer, NULL) == MH_ERROR_NONE);
	publication.reject = 0;
	CHECK(mh_data_stream_replace(stream, file, check_publication,
		&publication, NULL) == MH_ERROR_NONE);
	CHECK(publication.calls == 2);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(observer, NULL) == MH_ERROR_STATE);
	mh_data_stream_close(stream);
	CHECK(mh_data_directory_read(directory, FILE_NAME, 7, &bytes, &created,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 7 && memcmp(bytes.data, "updated", 7) == 0);
	free(bytes.data);
	mh_data_file_close(created);

	/*
	 * A stale target cannot consume the sealed stream: retry with current
	 * one.
	 */
	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_replace(stream, observer, NULL, NULL, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_data_stream_replace(stream, file, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	mh_data_stream_close(stream);
	mh_data_file_close(observer);
	CHECK(mh_data_directory_read(directory, FILE_NAME, 0, &bytes, &created,
		NULL) == MH_ERROR_NONE &&
		bytes.size == 0);
	free(bytes.data);
	mh_data_file_close(created);

	/*
	 * Recording-sized blocks cross buffer growth boundaries and retain
	 * order.
	 */
	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 17; i++) {
		memset(block, (int)i, sizeof(block));
		CHECK(mh_data_stream_append(stream, block, sizeof(block),
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_data_stream_size(stream) == 17 * sizeof(block));
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_replace(stream, file, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	mh_data_stream_close(stream);
	CHECK(mh_data_directory_read(directory, FILE_NAME, 17 * sizeof(block),
		&bytes, &created, NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 17 * sizeof(block));

	for (position = 0; position < bytes.size; position++)
		CHECK(bytes.data[position] == position / sizeof(block));

	free(bytes.data);
	mh_data_file_close(created);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);

	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "discard", 7, NULL) ==
		MH_ERROR_NONE);
	mh_data_stream_close(stream);
	mh_data_directory_close(other);
	mh_data_directory_close(directory);

	return 1;
}

static int
check_provider(void)
{
#ifdef MH_HAVE_USER_DATA_FILES
	const struct mh_user_data *data;
	char *root;
	char assets[256], directory[256];

	root = mh_test_directory("mh-data-file-");
	CHECK(root != NULL);
	sprintf(assets, "%s/assets", root);
	sprintf(directory, "%s/saves", root);
	CHECK(mkdir(assets, 0700) == 0 && mkdir(directory, 0700) == 0);
	data = mh_filesystem_user_data();
	CHECK(file_updates(data, directory, assets));
	CHECK(stream_updates(data, directory, assets));
	CHECK(rmdir(directory) == 0 && rmdir(assets) == 0 && rmdir(root) == 0);
#else
	const struct mh_user_data *data;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "saves", NULL) ==
		MH_ERROR_NONE);
	CHECK(file_updates(data, "saves", "installed-assets"));
	CHECK(stream_updates(data, "saves", "installed-assets"));
	mh_memory_user_data_close(data);
#endif

	return 1;
}

int
main(void)
{
	if (!check_provider())
		return 1;

	puts("Stored files: conditional replacement, policy failure, refreshed/stale identities, empty values, external replacement and private stream lifecycle passed");

	return 0;
}

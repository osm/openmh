#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_windows_path.h"
#include "interfaces/storage/data_stream.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr,                                        \
				"Windows data line %d: %s (%s; Win32 %lu)\n",  \
				__LINE__, #condition,                          \
				error.message == NULL ? "no diagnostic"        \
						      : error.message,         \
				(unsigned long)GetLastError());                \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_error error;

static int
read_value(struct mh_data_directory *directory, const char *name,
	const char *expected)
{
	struct mh_file bytes;
	struct mh_data_file *file;

	CHECK(mh_data_directory_read(directory, name, 4096, &bytes, &file,
		&error) == MH_ERROR_NONE);
	CHECK(bytes.size == strlen(expected) &&
		memcmp(bytes.data, expected, bytes.size) == 0);
	CHECK(mh_data_file_validate(file, &error) == MH_ERROR_NONE);
	free(bytes.data);
	mh_data_file_close(file);

	return 0;
}

static enum mh_error_code
reject_publication(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	(void)context;

	(void)directory;

	return mh_error_set(
		error_out, MH_ERROR_STATE, "fixture rejected publication");
}

static enum mh_directory_action
count_entries(void *context, const char *name)
{
	unsigned int *count;

	(void)name;
	count = context;
	(*count)++;

	return MH_DIRECTORY_CONTINUE;
}

static int
check_saved_files(const char *root, const char *userdata)
{
	const struct mh_user_data *storage;
	struct mh_data_directory *directory;
	struct mh_data_file *file, *stale, *unchanged;
	struct mh_file bytes;
	WCHAR *wide;
	HANDLE locked;
	char path[2048];
	unsigned int count;

	storage = mh_filesystem_user_data();
	sprintf(path, "%s/Player \303\245.dat", userdata);
	CHECK(mh_user_data_replace(storage, path, "original", 8, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(storage, path, 100, &bytes, &error) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 8 && memcmp(bytes.data, "original", 8) == 0);
	free(bytes.data);
	CHECK(mh_user_data_open_directory(storage, userdata, root, &directory,
		&error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_open_file(directory, "Player \303\245.dat",
		&file, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_open_file(directory, "Player \303\245.dat",
		&stale, &error) == MH_ERROR_NONE);
	CHECK(mh_data_file_replace(file, "new", 3, reject_publication, NULL,
		&error) == MH_ERROR_STATE);
	CHECK(read_value(directory, "Player \303\245.dat", "original") == 0);
	CHECK(mh_data_file_validate(stale, &error) == MH_ERROR_NONE);
	CHECK(mh_data_file_replace(file, "new", 3, NULL, NULL, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_validate(file, &error) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(stale, &error) == MH_ERROR_STATE);
	CHECK(mh_data_file_remove(stale, &error) == MH_ERROR_STATE);
	CHECK(mh_data_file_refresh(stale, &error) == MH_ERROR_STATE);
	CHECK(read_value(directory, "Player \303\245.dat", "new") == 0);
	mh_data_file_close(stale);

	unchanged = file;
	CHECK(mh_data_directory_create(directory, "Player \303\245.dat",
		"clobber", 7, NULL, NULL, &unchanged,
		&error) == MH_ERROR_EXISTS);
	CHECK(unchanged == file);
	CHECK(mh_windows_path_to_wide(path, &wide, &error) == MH_ERROR_NONE);
	locked = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, 0, NULL);
	CHECK(locked != INVALID_HANDLE_VALUE);
	CHECK(mh_data_file_replace(file, "locked", 6, NULL, NULL, &error) !=
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(storage, path, "locked", 6, &error) !=
		MH_ERROR_NONE);
	CHECK(CloseHandle(locked));
	CHECK(read_value(directory, "Player \303\245.dat", "new") == 0);
	CHECK(mh_data_file_validate(file, &error) == MH_ERROR_NONE);

	/*
	 * Editing the same entry changes its revision but permits explicit
	 * refresh.
	 */
	locked = CreateFileW(
		wide, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	CHECK(locked != INVALID_HANDLE_VALUE);
	CHECK(SetEndOfFile(locked));
	CHECK(CloseHandle(locked));
	CHECK(mh_data_file_validate(file, &error) == MH_ERROR_STATE);
	CHECK(mh_data_file_refresh(file, &error) == MH_ERROR_NONE);
	CHECK(mh_data_file_replace(file, "refreshed", 9, NULL, NULL, &error) ==
		MH_ERROR_NONE);
	CHECK(read_value(directory, "Player \303\245.dat", "refreshed") == 0);
	free(wide);

	count = 0;
	CHECK(mh_data_directory_list(directory, count_entries, &count,
		&error) == MH_ERROR_NONE);
	CHECK(count == 1);
	CHECK(mh_data_file_remove(file, &error) == MH_ERROR_NONE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);

	return 0;
}

static int
check_streams(const char *root, const char *userdata)
{
	struct mh_data_directory *directory;
	struct mh_data_file *file, *other;
	struct mh_data_stream *stream;
	unsigned int count;

	CHECK(mh_filesystem_data_directory_open(
		userdata, root, &directory, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "recording.dat", "old", 3,
		NULL, NULL, &file, &error) == MH_ERROR_NONE);
	CHECK(mh_data_stream_open(directory, &stream, &error) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "0000", 4, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "frames", 6, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_write_at(stream, 0, "HEAD", 4, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_data_stream_size(stream) == 10);
	CHECK(mh_data_stream_seal(stream, &error) == MH_ERROR_NONE);
	other = file;
	CHECK(mh_data_stream_create(stream, "recording.dat", NULL, NULL, &other,
		&error) == MH_ERROR_EXISTS);
	CHECK(other == file);
	CHECK(mh_data_stream_replace(stream, file, reject_publication, NULL,
		&error) == MH_ERROR_STATE);
	CHECK(read_value(directory, "recording.dat", "old") == 0);
	CHECK(mh_data_stream_replace(stream, file, NULL, NULL, &error) ==
		MH_ERROR_NONE);
	mh_data_stream_close(stream);
	CHECK(mh_data_file_validate(file, &error) == MH_ERROR_NONE);
	CHECK(read_value(directory, "recording.dat", "HEADframes") == 0);
	CHECK(mh_data_file_remove(file, &error) == MH_ERROR_NONE);
	mh_data_file_close(file);

	CHECK(mh_data_stream_open(directory, &stream, &error) == MH_ERROR_NONE);
	CHECK(mh_data_stream_append(stream, "abandoned", 9, &error) ==
		MH_ERROR_NONE);
	mh_data_stream_close(stream);
	CHECK(mh_data_stream_open(directory, &stream, &error) == MH_ERROR_NONE);
	CHECK(mh_data_stream_seal(stream, &error) == MH_ERROR_NONE);
	CHECK(mh_data_stream_create(stream, "empty.dat", NULL, NULL, &file,
		&error) == MH_ERROR_NONE);
	mh_data_stream_close(stream);
	CHECK(read_value(directory, "empty.dat", "") == 0);
	CHECK(mh_data_file_remove(file, &error) == MH_ERROR_NONE);
	mh_data_file_close(file);
	count = 0;
	CHECK(mh_data_directory_list(directory, count_entries, &count,
		&error) == MH_ERROR_NONE);
	CHECK(count == 0);
	mh_data_directory_close(directory);

	return 0;
}

static int
check_protection(const char *root, const char *userdata)
{
	struct mh_data_directory *directory, *other;
	struct mh_data_file *file;
	char *resolved;
	int same, removable;

	directory = NULL;
	CHECK(mh_filesystem_data_directory_open(
		root, root, &directory, &error) == MH_ERROR_PERMISSION);
	CHECK(directory == NULL);
	CHECK(mh_filesystem_data_directory_browse(
		root, root, 0, &directory, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_validate(directory, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "forbidden", "", 0, NULL,
		NULL, &file, &error) == MH_ERROR_PERMISSION);
	CHECK(mh_data_directory_inspect(directory, "userdata", &file,
		&removable, &error) == MH_ERROR_NONE);
	CHECK(file == NULL && !removable);
	mh_data_directory_close(directory);

	CHECK(mh_filesystem_data_directory_open(
		userdata, root, &directory, &error) == MH_ERROR_NONE);
	CHECK(mh_filesystem_data_directory_open(
		userdata, root, &other, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_same(directory, other, &same, &error) ==
			MH_ERROR_NONE &&
		same);
	CHECK(mh_data_directory_protect(directory, userdata, &error) ==
		MH_ERROR_PERMISSION);
	CHECK(mh_data_directory_validate(directory, &error) == MH_ERROR_NONE);
	CHECK(mh_filesystem_resource_directory(
		root, userdata, &resolved, &error) == MH_ERROR_NONE);
	free(resolved);
	resolved = NULL;
	CHECK(mh_filesystem_resource_directory(
		userdata, root, &resolved, &error) == MH_ERROR_INVALID);
	CHECK(resolved == NULL);
	mh_data_directory_close(other);
	mh_data_directory_close(directory);

	return 0;
}

static int
check_relocated_catalog(const char *root, const char *userdata)
{
	struct mh_data_directory *directory, *replacement;
	struct mh_data_file *file, *new_file;
	WCHAR *original, *moved, *entry;
	char moved_path[2048];
	unsigned int count;
	int same, result;

	sprintf(moved_path, "%s/Moved", root);
	CHECK(mh_windows_path_to_wide(userdata, &original, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_windows_path_to_wide(moved_path, &moved, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_filesystem_data_directory_browse(
		userdata, root, 0, &directory, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "entry", "held", 4, NULL,
		NULL, &file, &error) == MH_ERROR_NONE);
	CHECK(MoveFileW(original, moved));
	CHECK(CreateDirectoryW(original, NULL));
	CHECK(mh_filesystem_data_directory_open(
		userdata, root, &replacement, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(replacement, "entry", "replacement", 11,
		NULL, NULL, &new_file, &error) == MH_ERROR_NONE);
	CHECK(mh_data_directory_same(directory, replacement, &same, &error) ==
			MH_ERROR_NONE &&
		!same);
	CHECK(read_value(directory, "entry", "held") == 0);
	count = 0;
	CHECK(mh_data_directory_list(directory, count_entries, &count,
		&error) == MH_ERROR_NONE);
	CHECK(count == 1);
	CHECK(mh_data_file_replace(file, "no", 2, NULL, NULL, &error) ==
		MH_ERROR_STATE);
	CHECK(mh_data_file_remove(file, &error) == MH_ERROR_STATE);
	CHECK(mh_data_file_remove_entry(file, &error) == MH_ERROR_NONE);
	CHECK(read_value(replacement, "entry", "replacement") == 0);
	CHECK(mh_data_file_remove(new_file, &error) == MH_ERROR_NONE);
	mh_data_file_close(new_file);
	mh_data_file_close(file);
	mh_data_directory_close(replacement);
	mh_data_directory_close(directory);
	result = 0;
	entry = malloc(
		((size_t)lstrlenW(moved) + sizeof("\\entry")) * sizeof(*entry));
	CHECK(entry != NULL);
	lstrcpyW(entry, moved);
	lstrcatW(entry, L"\\entry");

	if (GetFileAttributesW(entry) != INVALID_FILE_ATTRIBUTES) {
		/*
		 * Wine 10 retains a stale unix_name on a separately renamed
		 * directory.
		 */
		CHECK(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
			"wine_get_version") != NULL);
		CHECK(DeleteFileW(entry));
		puts("SKIP: Wine retained the renamed catalog entry; repeat this test on native Windows");
		result = 77;
	}

	free(entry);
	CHECK(RemoveDirectoryW(moved));
	free(original);
	free(moved);

	return result;
}

int
main(int argc, char *argv[])
{
	WCHAR temp[MAX_PATH], base[MAX_PATH], root[1024], userdata[1024];
	char *root_utf8, *userdata_utf8;
	int result;

	CHECK(GetTempPathW(MAX_PATH, temp) != 0);
	CHECK(GetTempFileNameW(temp, L"omh", 0, base) != 0);
	CHECK(DeleteFileW(base));
	CHECK(CreateDirectoryW(base, NULL));
	lstrcpyW(root, base);
	lstrcatW(root, L"\\OpenMH \x00e5\x00e4\x00f6");
	CHECK(CreateDirectoryW(root, NULL));
	lstrcpyW(userdata, root);
	lstrcatW(userdata, L"\\userdata");
	CHECK(CreateDirectoryW(userdata, NULL));
	CHECK(mh_windows_path_to_utf8(root, &root_utf8, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_windows_path_to_utf8(userdata, &userdata_utf8, &error) ==
		MH_ERROR_NONE);
	CHECK(strlen(userdata_utf8) < 1800);
	result = 0;

	if (argc == 2 && strcmp(argv[1], "--relocation") == 0)
		result = check_relocated_catalog(root_utf8, userdata_utf8);
	else {
		CHECK(argc == 1);
		CHECK(check_saved_files(root_utf8, userdata_utf8) == 0);
		CHECK(check_streams(root_utf8, userdata_utf8) == 0);
		CHECK(check_protection(root_utf8, userdata_utf8) == 0);
	}

	free(root_utf8);
	free(userdata_utf8);
	CHECK(RemoveDirectoryW(userdata));
	CHECK(RemoveDirectoryW(root));
	CHECK(RemoveDirectoryW(base));

	if (result == 0)
		puts("Windows userdata fixture passed");

	return result;
}

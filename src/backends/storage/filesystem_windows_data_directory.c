#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_platform.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_windows_data_directory_internal.h"
#include "engine/storage/path_names.h"

static int
protected_asset_path(const WCHAR *path, const WCHAR *root)
{
	const WCHAR *relative;
	int length;

	if (root == NULL)
		return 0;

	relative = mh_windows_relative_path(path, root);

	if (relative == NULL)
		return 0;

	length = lstrlenW(L"" MH_USER_DATA_DIR);

	return lstrlenW(relative) < length ||
		CompareStringOrdinal(relative, length, L"" MH_USER_DATA_DIR,
			length, TRUE) != CSTR_EQUAL ||
		(relative[length] != 0 && relative[length] != L'\\');
}

enum mh_error_code
mh_windows_data_validate(void *context, struct mh_error *error_out)
{
	struct native_directory *directory;
	BY_HANDLE_FILE_INFORMATION held, named;
	HANDLE handle;
	WCHAR *path;
	enum mh_error_code status;

	directory = context;

	if (directory->read_only)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"directory is read-only");

	status = mh_windows_open_directory(directory->original_path,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &handle,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(handle, &named, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_handle_info(
			directory->handle, &held, error_out);

	if (status == MH_ERROR_NONE && !mh_windows_same_file(&held, &named))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"writable directory changed");

	path = NULL;

	if (status == MH_ERROR_NONE)
		status = mh_windows_handle_path(handle, &path, error_out);

	if (status == MH_ERROR_NONE &&
		protected_asset_path(path, directory->protected_root))
		status = mh_error_set(error_out, MH_ERROR_PERMISSION,
			"cannot write user data in the original asset tree");

	free(path);
	CloseHandle(handle);

	return status;
}

enum mh_error_code
mh_windows_data_readable(void *context, struct mh_error *error_out)
{
	struct native_directory *directory;
	BY_HANDLE_FILE_INFORMATION info;

	directory = context;

	if (!directory->browsing)
		return mh_windows_data_validate(context, error_out);

	return mh_windows_handle_info(directory->handle, &info, error_out);
}

static enum mh_error_code
list_directory(void *context, mh_directory_visit visit, void *visitor,
	struct mh_error *error_out)
{
	struct native_directory *directory;
	enum mh_error_code status;

	directory = context;
	status = mh_windows_data_readable(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_windows_list_directory(
		directory->handle, visit, visitor, error_out);
}

enum mh_error_code
mh_windows_data_publish(struct native_directory *directory,
	struct mh_windows_temporary *temporary, struct native_file *file,
	int replace, mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	WCHAR *destination;
	enum mh_error_code status;

	status = mh_windows_data_validate(directory, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = mh_windows_data_file_validate(
			directory, file, error_out);

	if (status == MH_ERROR_NONE && check != NULL)
		status = check(checker, owner, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_data_validate(directory, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = mh_windows_data_file_validate(
			directory, file, error_out);

	destination = NULL;

	if (status == MH_ERROR_NONE)
		status = mh_windows_path_to_wide(
			file->name, &destination, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_temporary_publish(temporary,
			directory->handle, destination, replace, error_out);

	if (status == MH_ERROR_NONE)
		file->identity = temporary->identity;

	free(destination);

	return status;
}

static enum mh_error_code
publish_bytes(struct native_directory *directory, struct native_file *file,
	const void *bytes, size_t size, int replace,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	struct mh_windows_temporary temporary;
	enum mh_error_code status;

	status = mh_windows_data_validate(directory, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = mh_windows_data_file_validate(
			directory, file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_temporary_open(
		directory->handle, &temporary, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_write_bytes(
		temporary.handle, bytes, size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_temporary_seal(&temporary, size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_data_publish(directory, &temporary, file,
			replace, check, checker, owner, error_out);

	mh_windows_temporary_close(&temporary);

	return status;
}

static enum mh_error_code
create_file(void *context, const char *name, const void *bytes, size_t size,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, void **output,
	struct mh_error *error_out)
{
	struct native_file *file;
	enum mh_error_code status;

	file = mh_windows_data_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating stored file");

	status = publish_bytes(context, file, bytes, size, 0, check, checker,
		owner, error_out);

	if (status != MH_ERROR_NONE) {
		mh_windows_data_file_close(NULL, file);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_file(void *context, void *file, const void *bytes, size_t size,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	return publish_bytes(context, file, bytes, size, 1, check, checker,
		owner, error_out);
}

static enum mh_error_code
protect_directory(
	void *context, const char *resource_root, struct mh_error *error_out)
{
	struct native_directory *directory;
	WCHAR *root, *previous;
	enum mh_error_code status;

	directory = context;
	status = mh_windows_resolve_resource(resource_root, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	previous = directory->protected_root;
	directory->protected_root = root;
	status = mh_windows_data_validate(directory, error_out);

	if (status != MH_ERROR_NONE) {
		directory->protected_root = previous;
		free(root);
		return status;
	}

	free(previous);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const char *
directory_path(void *context)
{
	struct native_directory *directory;

	directory = context;

	return directory->path;
}

static enum mh_error_code
same_directory(
	void *context, void *other, int *same, struct mh_error *error_out)
{
	struct native_directory *first, *second;
	BY_HANDLE_FILE_INFORMATION first_info, second_info;
	enum mh_error_code status;

	first = context;
	second = other;
	status = mh_windows_handle_info(first->handle, &first_info, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_handle_info(
			second->handle, &second_info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*same = mh_windows_same_file(&first_info, &second_info);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_directory(void *context)
{
	struct native_directory *directory;

	directory = context;

	if (directory->handle != INVALID_HANDLE_VALUE)
		CloseHandle(directory->handle);

	free(directory->path);
	free(directory->original_path);
	free(directory->protected_root);
	free(directory);
}

static enum mh_error_code
open_directory(const char *path, const char *protected_root, int browsing,
	int read_only, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	static const struct mh_data_browse_ops browse = {directory_path,
		same_directory, mh_windows_data_inspect,
		mh_windows_data_remove_entry};

	static const struct mh_data_directory_ops ops = {
		mh_windows_data_readable, list_directory, mh_windows_data_read,
		create_file, mh_windows_data_remove, mh_windows_data_file_close,
		close_directory, mh_windows_data_file_validate, replace_file,
		mh_windows_data_open_file, &mh_windows_data_stream_ops,
		protect_directory, mh_windows_data_file_refresh, &browse};

	struct native_directory *directory;
	enum mh_error_code status;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory paths");

	directory = calloc(1, sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening writable directory");

	directory->handle = INVALID_HANDLE_VALUE;
	directory->browsing = browsing;
	directory->read_only = read_only;
	status = mh_windows_resolve_directory(
		path, &directory->original_path, error_out);

	if (status == MH_ERROR_NONE && protected_root != NULL)
		status = mh_windows_resolve_resource(
			protected_root, &directory->protected_root, error_out);

	if (status == MH_ERROR_NONE && browsing &&
		protected_asset_path(
			directory->original_path, directory->protected_root))
		directory->read_only = 1;

	if (status == MH_ERROR_NONE)
		status = mh_windows_open_directory(directory->original_path,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			&directory->handle, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_path_to_utf8(
			directory->original_path, &directory->path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_data_readable(directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_bind(
			&ops, directory, output, error_out);

	if (status != MH_ERROR_NONE)
		close_directory(directory);

	return status;
}

enum mh_error_code
mh_filesystem_data_directory_open(const char *path, const char *protected_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	return open_directory(path, protected_root, 0, 0, output, error_out);
}

enum mh_error_code
mh_filesystem_data_directory_browse(const char *path,
	const char *protected_root, int read_only,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	if (protected_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing catalog resource root");

	return open_directory(
		path, protected_root, 1, read_only != 0, output, error_out);
}

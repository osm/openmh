#include "backends/storage/filesystem_windows_staging_cleanup.h"

#define CLEANUP_DEPTH_LIMIT 64

struct cleanup_directory {
	HANDLE			handle;
	unsigned int		depth;
	enum mh_error_code	status;
	struct mh_error		*error_out;
};

static enum mh_error_code	remove_tree(HANDLE, unsigned int,
	struct mh_error *);

static enum mh_directory_action
remove_entry(void *context, const char *name)
{
	struct cleanup_directory *directory;
	HANDLE child;

	directory = context;
	directory->status = mh_windows_open_child(directory->handle, name,
		DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
		&child, directory->error_out);

	if (directory->status == MH_ERROR_NONE) {
		directory->status = remove_tree(
			child, directory->depth + 1, directory->error_out);
		CloseHandle(child);
	}

	return directory->status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
						: MH_DIRECTORY_STOP;
}

static enum mh_error_code
remove_tree(HANDLE handle, unsigned int depth, struct mh_error *error_out)
{
	struct cleanup_directory directory;
	BY_HANDLE_FILE_INFORMATION info;
	struct mh_error scan_error;
	enum mh_error_code status;

	if (depth > CLEANUP_DEPTH_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"private directory tree is too deep to remove");

	status = mh_windows_handle_info(handle, &info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		directory.handle = handle;
		directory.depth = depth;
		directory.status = MH_ERROR_NONE;
		directory.error_out = error_out;
		status = mh_windows_list_directory(
			handle, remove_entry, &directory, &scan_error);

		if (directory.status != MH_ERROR_NONE)
			return directory.status;

		if (status != MH_ERROR_NONE)
			return mh_error_forward(error_out, &scan_error);
	}

	return mh_windows_delete_handle(handle, error_out);
}

enum mh_error_code
mh_windows_staging_remove(HANDLE directory, struct mh_error *error_out)
{
	return remove_tree(directory, 0, error_out);
}

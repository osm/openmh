#ifndef MH_FILESYSTEM_WINDOWS_HANDLE_H
#define MH_FILESYSTEM_WINDOWS_HANDLE_H

#include "backends/storage/filesystem_windows_path.h"
#include "interfaces/storage/directory.h"

int			mh_windows_same_file(
	const BY_HANDLE_FILE_INFORMATION *first,
	const BY_HANDLE_FILE_INFORMATION *second);
int			mh_windows_same_revision(
	const BY_HANDLE_FILE_INFORMATION *first,
	const BY_HANDLE_FILE_INFORMATION *second);
int			mh_windows_regular_file(
	const BY_HANDLE_FILE_INFORMATION *info);
enum mh_error_code	mh_windows_handle_info(HANDLE handle,
	BY_HANDLE_FILE_INFORMATION *output, struct mh_error *error_out);
enum mh_error_code	mh_windows_handle_path(HANDLE handle, WCHAR **output,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_open_directory(const WCHAR *path,
	DWORD sharing, HANDLE *output, struct mh_error *error_out);

/*
 * Independent handle/cursor for the same directory, including after a rename.
 */
enum mh_error_code	mh_windows_reopen_directory(HANDLE directory,
	DWORD access, DWORD sharing, HANDLE *output,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_resolve_directory(const char *path,
	WCHAR **output, struct mh_error *error_out);

/* Resolve a protected asset root, which can be a directory or package file. */
enum mh_error_code	mh_windows_resolve_resource(const char *path,
	WCHAR **output, struct mh_error *error_out);

/* Relative opens retain the held directory's identity after a rename. */
enum mh_error_code	mh_windows_open_child(HANDLE directory,
	const char *name, DWORD access, DWORD sharing, int create,
	HANDLE *output, struct mh_error *error_out);
enum mh_error_code	mh_windows_create_child_directory(HANDLE directory,
	const char *name, DWORD access, DWORD sharing, HANDLE *output,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_list_directory(HANDLE directory,
	mh_directory_visit visit, void *visitor, struct mh_error *error_out);

/* Returns the relative suffix, or NULL when outside the canonical root. */
const WCHAR	       *mh_windows_relative_path(const WCHAR *path,
	const WCHAR *root);
enum mh_error_code	mh_windows_write_bytes(HANDLE handle, const void *bytes,
	size_t size, struct mh_error *error_out);
enum mh_error_code	mh_windows_seek_file(HANDLE handle, size_t position,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_delete_handle(HANDLE handle,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_rename_handle(HANDLE handle,
	HANDLE directory, const WCHAR *path, int replace,
	struct mh_error *error_out);

#endif

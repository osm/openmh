#ifndef MH_FILESYSTEM_WINDOWS_DATA_DIRECTORY_INTERNAL_H
#define MH_FILESYSTEM_WINDOWS_DATA_DIRECTORY_INTERNAL_H

#include "backends/storage/filesystem_windows_temporary.h"
#include "interfaces/storage/data_stream_backend.h"

struct native_directory {
	HANDLE	handle;
	WCHAR	*original_path, *protected_root;
	char	*path;
	int	browsing, read_only;
};

struct native_file {
	char				*name;
	BY_HANDLE_FILE_INFORMATION	identity;
};

struct native_file	*mh_windows_data_file_new(const char *name);
void			 mh_windows_data_file_close(void *context, void *token);
enum mh_error_code	 mh_windows_data_validate(void *context,
	struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_readable(void *context,
	struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_open_file(void *context,
	const char *name, void **output, struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_read(void *context, const char *name,
	size_t limit, struct mh_file *bytes, void **output,
	struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_file_validate(void *context,
	void *token, struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_file_refresh(void *context,
	void *token, struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_inspect(void *context,
	const char *name, void **output, int *removable,
	struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_remove(void *context, void *token,
	struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_remove_entry(void *context,
	void *token, struct mh_error *error_out);
enum mh_error_code	 mh_windows_data_publish(
	struct native_directory *directory,
	struct mh_windows_temporary *temporary, struct native_file *file,
	int replace, mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out);
extern const struct mh_data_stream_ops mh_windows_data_stream_ops;

#endif

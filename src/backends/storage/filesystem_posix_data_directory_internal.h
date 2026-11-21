#ifndef MH_FILESYSTEM_DATA_DIRECTORY_INTERNAL_H
#define MH_FILESYSTEM_DATA_DIRECTORY_INTERNAL_H

#include <sys/stat.h>

#include "interfaces/storage/data_stream_backend.h"

#define TEMP_NAME_SIZE 96

/* Owns canonical paths and the held directory descriptor. */
struct native_directory {
	char		*path, *protected_root;
	int		fd, browsing, read_only;
	unsigned long	serial;
};

/* Owns its basename; identity is the last accepted file revision. */
struct native_file {
	char		*name;
	struct stat	identity;
};

/* Records a staged entry so cleanup only removes the captured inode. */
struct native_temporary {
	char		name[TEMP_NAME_SIZE];
	struct stat	identity;
};

struct native_file	*mh_filesystem_file_new(const char *name);
void			 mh_filesystem_file_close(void *context, void *token);
enum mh_error_code	 mh_filesystem_temporary_open(
	struct native_directory *directory, struct native_temporary *temporary,
	int *output, struct mh_error *error_out);
enum mh_error_code	 mh_filesystem_temporary_publish(
	struct native_directory *directory, struct native_temporary *temporary,
	struct native_file *file, int replace, mh_data_publish_check check,
	void *checker, struct mh_data_directory *owner,
	struct mh_error *error_out);
void			 mh_filesystem_temporary_remove(
	struct native_directory *directory,
	const struct native_temporary *temporary);
extern const struct mh_data_stream_ops mh_filesystem_stream_ops;

/* Publish a new name without replacing any existing directory entry. */
int	mh_filesystem_publish_new(int directory, const char *temporary,
	const char *name);

#endif

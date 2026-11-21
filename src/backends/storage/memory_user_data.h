#ifndef MH_BACKENDS_STORAGE_MEMORY_USER_DATA_H
#define MH_BACKENDS_STORAGE_MEMORY_USER_DATA_H

#include "interfaces/storage/user_data_backend.h"

/*
 * Empty mutable view with exact, case-sensitive keys. Owns copies of keys and
 * bytes; read copies may outlive it. No files, threads or durable persistence.
 * Keys use relative slash-separated components, without empty components;
 * directory operations also accept a trailing slash. Parent directories are
 * implicit when writing a key; explicit empty directories are retained. Files
 * and directories cannot share a key, and a file cannot be a parent.
 * Close only after all users finish. Failed open leaves output unchanged.
 */
enum mh_error_code	mh_memory_user_data_open(
	const struct mh_user_data **output, struct mh_error *error_out);
void			mh_memory_user_data_close(
	const struct mh_user_data *data);

#endif

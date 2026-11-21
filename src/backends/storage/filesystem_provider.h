#ifndef MH_BACKENDS_STORAGE_FILESYSTEM_PROVIDER_H
#define MH_BACKENDS_STORAGE_FILESYSTEM_PROVIDER_H

#include "interfaces/storage/data_directory_backend.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/storage.h"
#include "interfaces/storage/user_data_backend.h"

/*
 * Stateless native reader. Selection belongs to application host setup.
 * Filesystem locators use UTF-8 on Windows; resource bytes are never recoded.
 */
const struct mh_resource_source	*mh_filesystem_resources(void);

/*
 * Directory of the running executable, independent of the working directory.
 * Output is unchanged on failure. Native host startup uses this default.
 */
enum mh_error_code	mh_filesystem_executable_directory(char *output,
	size_t capacity, struct mh_error *error_out);

/*
 * Native compatibility composition: resolve a source path inside the canonical
 * asset root. The caller owns the result. Does not open a catalog or write.
 * Available with the native filesystem provider; failures preserve output.
 */
enum mh_error_code	mh_filesystem_resource_directory(const char *asset_root,
	const char *directory, char **output, struct mh_error *error_out);

/*
 * Filesystem selection; no I/O until an operation is requested. The native
 * default user directory is resolved/created by the directory operation.
 * Reads accept regular files (including symlink targets)
 * and reject a length change during the read.
 * Writes use a private sibling temporary, flush/sync and rename; a
 * destination symlink is replaced. Native POSIX commits retry pending
 * directory syncs.
 */
const struct mh_user_data	*mh_filesystem_user_data(void);

/*
 * Native directory must already exist, outside the protected asset tree.
 * NULL protected_root is reserved for native diagnostic callers which have
 * no resource namespace; ordinary user-data opens require a resource root.
 */
enum mh_error_code	mh_filesystem_data_directory_open(const char *path,
	const char *protected_root, struct mh_data_directory **output,
	struct mh_error *error_out);

/*
 * Readable held directory for catalogs. No creation. A protected-tree path
 * can be opened read-only; read_only also forces that policy outside the tree.
 * Writable publication still enforces the ordinary original-path checks.
 */
enum mh_error_code	mh_filesystem_data_directory_browse(const char *path,
	const char *protected_root, int read_only,
	struct mh_data_directory **output, struct mh_error *error_out);

/*
 * Open the selected bundle's default service. Native commits complete
 * synchronously and report outstanding filesystem sync failures.
 */
enum mh_error_code	mh_storage_open(struct mh_storage **output,
	struct mh_error *error_out);

#endif

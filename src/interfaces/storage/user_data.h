#ifndef MH_USER_DATA_H
#define MH_USER_DATA_H

#include "engine/core/file.h"
#include "interfaces/storage/directory.h"

struct mh_user_data;
struct mh_data_directory;

/*
 * Operations on the prepared writable view, separate from packaged resources.
 * Keys are nonempty host-selected names; a provider may use native paths or
 * logical names. Callers serialize access and retain the provider through use.
 * Read returns an independently owned allocation, even for an empty value.
 * Missing keys return NOT_FOUND; errors leave output unchanged. limit bounds
 * accepted bytes and must be smaller than the maximum size_t value.
 */
enum mh_error_code	mh_user_data_read(const struct mh_user_data *data,
	const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out);

/*
 * Atomically replace/create one value in the local view. Failure preserves
 * the previous value (or absence). Input bytes remain caller-owned; the
 * provider must finish consuming them before returning. NULL bytes are valid
 * only for size zero. Native parent directories must already exist.
 * Success does not promise durable storage: the host's mh_storage COMMIT
 * publishes local changes. A failed COMMIT must retain them for retry without
 * repeating this mutation or its game transaction.
 */
enum mh_error_code	mh_user_data_replace(const struct mh_user_data *data,
	const char *key, const void *bytes, size_t size,
	struct mh_error *error_out);

/*
 * Resolve/create the provider's default writable root after PREPARE. The
 * caller owns the returned allocation. An empty string denotes a logical
 * root without a prefix; nonempty roots use slash separators. Failure leaves
 * output unchanged. Providers without this operation report UNSUPPORTED;
 * explicit keys remain usable. This does not select or mutate a save file.
 */
enum mh_error_code	mh_user_data_directory(const struct mh_user_data *data,
	char **output, struct mh_error *error_out);

/*
 * Optional directory operations on the prepared view. Empty path denotes the
 * logical root (the working directory for native relative paths). Listing
 * visits immediate child names, including directories, once each, unsorted;
 * names are borrowed only during the callback. STOP succeeds, FAIL reports IO.
 * A failed scan may already have visited entries. Do not mutate/close the
 * provider from a visitor. Missing directories report NOT_FOUND.
 * Creation includes missing parents; existing directories succeed. Failure
 * may leave newly created parents. Neither operation promises durability.
 */
enum mh_error_code	mh_user_data_list(const struct mh_user_data *data,
	const char *path, mh_directory_visit visit, void *context,
	struct mh_error *error_out);
enum mh_error_code	mh_user_data_create_directory(
	const struct mh_user_data *data, const char *path,
	struct mh_error *error_out);

/*
 * Open an owned directory in this writable view after PREPARE. The caller
 * closes it before releasing data. resource_root identifies the immutable
 * asset tree for providers sharing its namespace (native filesystem paths).
 * Providers with disjoint resource/user namespaces need not interpret it.
 * The native provider rejects directories within that tree, except its
 * reserved userdata/ subtree, and retains their identity. Canonical paths
 * prevent a symlink in userdata/ from granting access to the asset tree.
 * Missing paths report NOT_FOUND; missing support reports UNSUPPORTED. Other
 * failures (including an invalid resource root) must not masquerade as an
 * absent writable directory. Failure preserves output.
 */
enum mh_error_code	mh_user_data_open_directory(
	const struct mh_user_data *data, const char *path,
	const char *resource_root, struct mh_data_directory **output,
	struct mh_error *error_out);

/*
 * Hold an existing directory for catalog inspection and reads, without creating
 * paths. Unlike writable open, a native path inside resource_root may succeed
 * read-only; file inspection reports that policy. Missing directories return
 * NOT_FOUND, absent support UNSUPPORTED. Other errors must not be treated as an
 * empty/read-only source. Close directory before data; failure preserves
 * output.
 */
enum mh_error_code	mh_user_data_browse_directory(
	const struct mh_user_data *data, const char *path,
	const char *resource_root, struct mh_data_directory **output,
	struct mh_error *error_out);

/*
 * Check that a directory exists without listing it or modifying the view.
 * Empty path denotes the logical root. Missing paths return NOT_FOUND;
 * non-directory nodes and other failures are errors. Missing support returns
 * UNSUPPORTED. This does not hold an identity or promise a subsequent open.
 */
enum mh_error_code	mh_user_data_check_directory(
	const struct mh_user_data *data, const char *path,
	struct mh_error *error_out);

#endif

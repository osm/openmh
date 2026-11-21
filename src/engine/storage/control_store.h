#ifndef MH_ENGINE_STORAGE_CONTROL_STORE_H
#define MH_ENGINE_STORAGE_CONTROL_STORE_H

#include "engine/core/error.h"
#include "engine/settings/control_catalog.h"

struct mh_control_store;
struct mh_resource_source;
struct mh_data_directory;

/*
 * Portable catalog owner. Borrow resources through close/reload and copy the
 * source-directory key. Adopt the optional writable directory only on success;
 * NULL gives session-only edits. Resources must provide read-only directory
 * access.
 *
 * Source profiles are overlaid by writable profiles with ASCII-folded names;
 * reject aliases, files over 1 MiB, total text over 32 MiB and more than 4096
 * profiles. Installed and writable files must be regular; native links are
 * rejected by the directory provider. A disappearing file supplies numbered
 * defaults with dirty=1. An empty catalog (including a missing installed
 * directory) supplies the built-in Keyboard profile, saved only to the writable
 * directory. catalog must be empty.
 *
 * Provider implementations own file identity and publication; no native paths
 * are required. Failed open leaves catalog/output and directory ownership
 * unchanged.
 */
enum mh_error_code	mh_control_store_open_from(
	const struct mh_resource_source *resources,
	const char *source_directory, struct mh_data_directory *directory,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_control_store **output, struct mh_error *error_out);

void	mh_control_store_close(struct mh_control_store *store);

/*
 * Rescan the same resource source and held writable directory and reselect by
 * display name. Reads only; replaces the catalog and store's file metadata on
 * success. Save pending edits before calling: a successful reload discards the
 * old in-memory profiles, including dirty ones. Errors preserve both objects.
 * selection may refer to a name in the old catalog.
 */
enum mh_error_code	mh_control_store_reload(struct mh_control_store **store,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_error *error_out);

/*
 * Save profiles whose dirty word is exactly 1, using their retained basename.
 * Validate every pending encoding/target before writing. Each file is flushed
 * privately and published atomically; successful profiles become clean.
 * Failures retain remaining dirty profiles for retry. This is not a batch
 * transaction or concurrent-writer synchronization. Replaced/edited files,
 * new case aliases and changes to the directory are rejected. A session-only
 * store returns success without clearing dirty words or writing anything.
 * The caller owns catalog and must preserve its count and profile order.
 */
enum mh_error_code	mh_control_store_save(struct mh_control_store *store,
	struct mh_control_catalog *catalog, struct mh_error *error_out);

/*
 * Retain catalog->count marks without modifying files. Empty selections fail.
 */
enum mh_error_code	mh_control_store_restrict(
	struct mh_control_store *store, struct mh_control_catalog *catalog,
	const unsigned char *keep, struct mh_error *error_out);

#endif

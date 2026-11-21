#ifndef MH_FILESYSTEM_STAGING_H
#define MH_FILESYSTEM_STAGING_H

#include "engine/core/error.h"

struct mh_filesystem_staging;

/*
 * Tool composition: create a private sibling of a new destination.
 * The parent directory must exist. Existing files, directories and symlinks
 * are rejected. path stays borrowed until close and is the only writable tree
 * owned by this transaction. Publish moves the complete tree without replacing
 * any destination, including one created after open. Close attempts to remove
 * unpublished output; a killed process may leave its private staging behind.
 * This promises local atomic publication, not crash-durable directory fsync.
 */
enum mh_error_code	mh_filesystem_staging_open(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out);

/*
 * Prepare replacement of an existing directory. Caller copies its contents into
 * staging and keeps writers stopped until publication completes.
 *
 * Linux atomically exchanges the trees. Windows renames the old directory to
 * DESTINATION.openmotorex-backup, then installs the new tree. That handoff is
 * NOT atomic: the destination can briefly be absent. On failure it attempts
 * rollback; if rollback fails, the original remains in that named backup.
 *
 * An existing backup blocks another update. If destination is missing, restore
 * the backup by renaming it back; otherwise verify destination before removing
 * the backup. Close attempts to remove retired data only after success.
 *
 * Browser workers support package updates only; directory updates return
 * MH_ERROR_UNSUPPORTED. No crash-durability promise; interrupted extraction may
 * leave private output.
 */
enum mh_error_code	mh_filesystem_staging_update(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out);

/*
 * Create a private workspace beside a path, whether that path exists or not.
 * The sibling is never opened or modified. Scratch trees cannot be published;
 * close removes them. Used to prepare replacement package contents.
 */
enum mh_error_code	mh_filesystem_staging_scratch(const char *sibling,
	struct mh_filesystem_staging **output, struct mh_error *error_out);

/*
 * Remove one immediate entry recursively from unpublished staging only.
 * Missing entries succeed. Links are removed without following their targets.
 */
enum mh_error_code	mh_filesystem_staging_remove(
	struct mh_filesystem_staging *staging, const char *name,
	struct mh_error *error_out);

const char	       *mh_filesystem_staging_path(
	const struct mh_filesystem_staging *staging);
enum mh_error_code	mh_filesystem_staging_publish(
	struct mh_filesystem_staging *staging, struct mh_error *error_out);
void			mh_filesystem_staging_close(
	struct mh_filesystem_staging *staging);

#endif

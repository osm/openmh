#ifndef MH_ASSET_VFS_H
#define MH_ASSET_VFS_H

#include "interfaces/storage/resource_archive.h"

struct mh_vfs;

/*
 * Mount exactly the selected directory or archive. Content discovery and
 * precedence are owned by the caller; no package filenames are special here.
 * The source/provider must outlive the mount. Close dependent readers first.
 */
enum mh_error_code		 mh_vfs_open(
	const struct mh_resource_source *source,
	const struct mh_resource_archive_provider *archives,
	const char *location, struct mh_vfs **output,
	struct mh_error *error_out);
const struct mh_resource_source	*mh_vfs_resources(const struct mh_vfs *vfs);
const char			*mh_vfs_root(const struct mh_vfs *vfs);

/*
 * Directory installation, or the package's parent, for native save placement.
 */
const char	*mh_vfs_installation(const struct mh_vfs *vfs);

/* On success takes ownership of all mounts; on failure retains none. */
enum mh_error_code	mh_vfs_combine(struct mh_vfs **mounts,
	const int *replace, size_t count, struct mh_vfs **output,
	struct mh_error *error_out);
void			mh_vfs_close(struct mh_vfs *vfs);

#endif

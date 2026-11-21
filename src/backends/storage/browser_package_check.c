#include <emscripten/emscripten.h>
#include <stdio.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "engine/assets/packages.h"

/*
 * Validate the prepared collection before the browser replaces remembered
 * assets. Keep game/patch compatibility policy in the shared content loader.
 */
EMSCRIPTEN_KEEPALIVE int
mh_browser_check_packages(void)
{
	struct mh_vfs *mount;
	struct mh_content_info info;
	struct mh_error error;
	enum mh_error_code status;

	mount = NULL;
	status = mh_content_mount(mh_filesystem_resources(),
		mh_zip_resource_provider(), "/validation", &mount, &error);

	if (status == MH_ERROR_NONE)
		status = mh_content_probe(mh_vfs_resources(mount),
			mh_vfs_root(mount), NULL, &info, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_vfs_close(mount);

	return status != MH_ERROR_NONE;
}

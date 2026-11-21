#include <stdlib.h>

#include "backends/archive/unshield_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/source/installer.h"

enum mh_error_code
mh_extract_installer_components(const void *bytes, size_t size,
	const char *staging, const char *relative, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	struct mh_archive *archive;
	char *root;
	enum mh_error_code status;

	archive = NULL;
	root = NULL;
	status = mh_unshield_open_components(bytes, size, &archive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_join(staging, relative, &root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_unpack(archive, root, MH_EXTRACT_INSTALLER,
			progress, context, error_out);

	free(root);
	mh_archive_close(archive);

	return status;
}

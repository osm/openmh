#include <string.h>

#include "backends/storage/filesystem_staging_internal.h"

enum mh_error_code
mh_filesystem_staging_remove(struct mh_filesystem_staging *staging,
	const char *name, struct mh_error *error_out)
{
	if (staging == NULL || name == NULL || name[0] == 0 ||
		strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
		strpbrk(name, "/\\:") != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"staging removal requires an immediate entry name");

	return mh_filesystem_staging_remove_entry(staging, name, error_out);
}

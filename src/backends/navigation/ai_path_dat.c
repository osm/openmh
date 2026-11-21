#include <stdlib.h>

#include "backends/navigation/internal.h"
#include "engine/assets/ai_path.h"

enum mh_error_code
mh_ai_path_dat_load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_ai_path *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = mh_navigation_resource_read(resources, root, reference,
		MH_RESOURCE_READ_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ai_path_dat_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

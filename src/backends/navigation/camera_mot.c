#include <stdlib.h>

#include "backends/navigation/internal.h"
#include "engine/assets/camera_motion.h"

enum mh_error_code
mh_camera_mot_load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference,
	struct mh_camera_motion *output, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = mh_navigation_resource_read(resources, root, reference,
		MH_RESOURCE_READ_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_camera_mot_parse(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

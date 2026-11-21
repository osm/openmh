#include <stdlib.h>

#include "backends/geometry/lob_provider.h"
#include "engine/assets/lob.h"
#include "engine/storage/asset_path.h"
#include "interfaces/geometry/wireframe_source_backend.h"
#include "interfaces/storage/resource.h"

#define WIREFRAME_PATH_BYTES 1024

static enum mh_error_code
load_wireframe(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_wireframe *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[WIREFRAME_PATH_BYTES];
	enum mh_error_code status;

	(void)context;
	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_wireframe_lob_decode(
		file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_wireframe_source *
mh_wireframe_lob_source(void)
{
	static const struct mh_wireframe_source source = {NULL, load_wireframe};

	return &source;
}

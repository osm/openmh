#include <stdlib.h>

#include "backends/animation/lightwave_provider.h"
#include "engine/assets/scene.h"
#include "engine/storage/asset_path.h"
#include "interfaces/animation/source_backend.h"
#include "interfaces/storage/resource.h"

#define ASSET_PATH_BYTES 1024

static enum mh_error_code
read_asset(const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *file, struct mh_error *error_out)
{
	char path[ASSET_PATH_BYTES];
	enum mh_error_code status;

	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read(resources, path, file, error_out);
}

static enum mh_error_code
load_scene(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_scene *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = read_asset(resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_scene_lws_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
load_motion(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_scene_object *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = read_asset(resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_motion_mot_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_animation_source *
mh_animation_lightwave_source(void)
{
	static const struct mh_animation_source source = {
		NULL, load_scene, load_motion};

	return &source;
}

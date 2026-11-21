#include <stddef.h>

#include "interfaces/animation/source_backend.h"

enum mh_error_code
mh_scene_load_from(const struct mh_animation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_scene *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_scene == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"scene loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene load request");

	return source->load_scene(
		source->context, resources, root, reference, output, error_out);
}

enum mh_error_code
mh_motion_load_from(const struct mh_animation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_scene_object *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_motion == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"motion loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid motion load request");

	return source->load_motion(
		source->context, resources, root, reference, output, error_out);
}

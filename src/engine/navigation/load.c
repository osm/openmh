#include <stddef.h>

#include "interfaces/navigation/source_backend.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_route_load_bounded_from(const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, size_t byte_limit, struct mh_route *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_route == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"route loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route load request");

	if (byte_limit > MH_RESOURCE_READ_LIMIT)
		byte_limit = MH_RESOURCE_READ_LIMIT;

	return source->load_route(source->context, resources, root, reference,
		byte_limit, output, error_out);
}

enum mh_error_code
mh_camera_motion_load_from(const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_camera_motion *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_camera == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"camera motion loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera motion load request");

	return source->load_camera(
		source->context, resources, root, reference, output, error_out);
}

enum mh_error_code
mh_ai_path_load_from(const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_ai_path *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_ai_path == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"ai path loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ai path load request");

	return source->load_ai_path(
		source->context, resources, root, reference, output, error_out);
}

enum mh_error_code
mh_route_load_from(const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_route *output,
	struct mh_error *error_out)
{
	return mh_route_load_bounded_from(source, resources, root, reference,
		MH_RESOURCE_READ_LIMIT, output, error_out);
}

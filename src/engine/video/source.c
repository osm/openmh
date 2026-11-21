#include "interfaces/video/source_backend.h"

static int
complete_provider(const struct mh_video_source_provider *provider)
{
	return provider != NULL && provider->ops != NULL &&
		provider->ops->resolve != NULL && provider->ops->read != NULL;
}

enum mh_error_code
mh_video_source_resolve(const struct mh_video_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *output, size_t capacity, int *found,
	struct mh_error *error_out)
{
	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"video source is unavailable");

	if (!complete_provider(provider) || resources == NULL || root == NULL ||
		reference == NULL || output == NULL || found == NULL ||
		capacity == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid video source lookup");

	return provider->ops->resolve(provider->context, resources, root,
		reference, output, capacity, found, error_out);
}

enum mh_error_code
mh_video_source_read(const struct mh_video_source_provider *provider,
	const struct mh_resource_source *resources, const char *reference,
	int collection, unsigned int index, struct mh_file *output,
	struct mh_error *error_out)
{
	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"video source is unavailable");

	if (!complete_provider(provider) || resources == NULL ||
		reference == NULL || output == NULL ||
		(collection != 0 && collection != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid video source read");

	return provider->ops->read(provider->context, resources, reference,
		collection, index, output, error_out);
}

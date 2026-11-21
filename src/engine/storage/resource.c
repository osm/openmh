#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_resource_source_read_at(const struct mh_resource_source *source,
	const char *path, unsigned long offset, void *output, size_t size,
	struct mh_error *error_out)
{
	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource range arguments");

	if (source->ops->read_at == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"resource range reads are unavailable");

	return source->ops->read_at(
		source->context, path, offset, output, size, error_out);
}

enum mh_error_code
mh_resource_source_init(struct mh_resource_source *source,
	const struct mh_resource_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (source == NULL || ops == NULL || ops->read == NULL ||
		ops->size == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid resource provider");

	source->ops = ops;
	source->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_resource_source_read(const struct mh_resource_source *source,
	const char *path, struct mh_file *output, struct mh_error *error_out)
{
	return mh_resource_source_read_bounded(
		source, path, MH_RESOURCE_READ_LIMIT, output, error_out);
}

enum mh_error_code
mh_resource_source_read_bounded(const struct mh_resource_source *source,
	const char *path, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource read arguments");

	if (limit > MH_RESOURCE_READ_LIMIT)
		limit = MH_RESOURCE_READ_LIMIT;

	return source->ops->read(
		source->context, path, limit, output, error_out);
}

enum mh_error_code
mh_resource_source_size(const struct mh_resource_source *source,
	const char *path, unsigned long *output, struct mh_error *error_out)
{
	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource size arguments");

	return source->ops->size(source->context, path, output, error_out);
}

enum mh_error_code
mh_resource_source_list(const struct mh_resource_source *source,
	const char *path, mh_directory_visit visit, void *context,
	struct mh_error *error_out)
{
	if (source == NULL || path == NULL || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource enumeration arguments");

	if (source->ops->list == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"resource enumeration is unavailable");

	return source->ops->list(
		source->context, path, visit, context, error_out);
}

enum mh_error_code
mh_resource_source_open_directory(const struct mh_resource_source *source,
	const char *path, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid resource directory open arguments");

	if (source->ops->open_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"held resource directories are unavailable");

	return source->ops->open_directory(
		source->context, path, output, error_out);
}

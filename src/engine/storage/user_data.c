#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_user_data_init(struct mh_user_data *data, const struct mh_user_data_ops *ops,
	void *context, struct mh_error *error_out)
{
	if (data == NULL || ops == NULL || ops->read == NULL ||
		ops->replace == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data provider");

	data->ops = ops;
	data->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_user_data_read(const struct mh_user_data *data, const char *key,
	size_t limit, struct mh_file *output, struct mh_error *error_out)
{
	if (data == NULL || key == NULL || key[0] == 0 || output == NULL ||
		limit == (size_t)-1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data read arguments");

	return data->ops->read(data->context, key, limit, output, error_out);
}

enum mh_error_code
mh_user_data_replace(const struct mh_user_data *data, const char *key,
	const void *bytes, size_t size, struct mh_error *error_out)
{
	if (data == NULL || key == NULL || key[0] == 0 ||
		(bytes == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data replacement arguments");

	return data->ops->replace(data->context, key,
		bytes == NULL ? "" : bytes, size, error_out);
}

enum mh_error_code
mh_user_data_directory(const struct mh_user_data *data, char **output,
	struct mh_error *error_out)
{
	if (data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data directory arguments");

	if (data->ops->directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"default user-data directory is unavailable");

	return data->ops->directory(data->context, output, error_out);
}

enum mh_error_code
mh_user_data_list(const struct mh_user_data *data, const char *path,
	mh_directory_visit visit, void *context, struct mh_error *error_out)
{
	if (data == NULL || path == NULL || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data listing arguments");

	if (data->ops->list == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"user-data listing is unavailable");

	return data->ops->list(data->context, path, visit, context, error_out);
}

enum mh_error_code
mh_user_data_create_directory(const struct mh_user_data *data, const char *path,
	struct mh_error *error_out)
{
	if (data == NULL || path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data directory creation arguments");

	if (data->ops->create_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"user-data directory creation is unavailable");

	return data->ops->create_directory(data->context, path, error_out);
}

enum mh_error_code
mh_user_data_open_directory(const struct mh_user_data *data, const char *path,
	const char *resource_root, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	if (data == NULL || path == NULL || resource_root == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory open arguments");

	if (data->ops->open_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"owned writable directories are unavailable");

	return data->ops->open_directory(
		data->context, path, resource_root, output, error_out);
}

enum mh_error_code
mh_user_data_check_directory(const struct mh_user_data *data, const char *path,
	struct mh_error *error_out)
{
	if (data == NULL || path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data directory check");

	if (data->ops->check_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"user-data directory checks are unavailable");

	return data->ops->check_directory(data->context, path, error_out);
}

enum mh_error_code
mh_user_data_browse_directory(const struct mh_user_data *data, const char *path,
	const char *resource_root, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	if (data == NULL || path == NULL || resource_root == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid user-data catalog directory arguments");

	if (data->ops->browse_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"user-data catalog directories are unavailable");

	return data->ops->browse_directory(
		data->context, path, resource_root, output, error_out);
}

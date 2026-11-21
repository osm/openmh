#include <stdlib.h>
#include <string.h>

#include "engine/storage/user_root.h"
#include "interfaces/storage/user_data_backend.h"

struct user_root {
	struct mh_user_data		data;
	const struct mh_user_data	*source;
	char				*path;
};

static enum mh_error_code
read_value(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_read(root->source, key, limit, output, error_out);
}

static enum mh_error_code
replace_value(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_replace(root->source, key, bytes, size, error_out);
}

static enum mh_error_code
default_directory(void *context, char **output, struct mh_error *error_out)
{
	struct user_root *root;
	char *path;
	size_t length;
	enum mh_error_code status;

	root = context;
	length = strlen(root->path) + 1;
	path = malloc(length);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying user-data root");

	memcpy(path, root->path, length);
	status = mh_user_data_create_directory(root->source, path, error_out);

	if (status == MH_ERROR_NONE)
		*output = path;
	else
		free(path);

	return status;
}

static enum mh_error_code
list_directory(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_list(root->source, path, visit, visitor, error_out);
}

static enum mh_error_code
create_directory(void *context, const char *path, struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_create_directory(root->source, path, error_out);
}

static enum mh_error_code
open_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_open_directory(
		root->source, path, resource_root, output, error_out);
}

static enum mh_error_code
check_directory(void *context, const char *path, struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_check_directory(root->source, path, error_out);
}

static enum mh_error_code
browse_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct user_root *root;

	root = context;

	return mh_user_data_browse_directory(
		root->source, path, resource_root, output, error_out);
}

enum mh_error_code
mh_user_root_open(const struct mh_user_data *source, const char *path,
	const struct mh_user_data **output, struct mh_error *error_out)
{
	static const struct mh_user_data_ops operations = {read_value,
		replace_value, default_directory, list_directory,
		create_directory, open_directory, check_directory,
		browse_directory};

	struct user_root *root;
	size_t length;
	enum mh_error_code status;

	if (source == NULL || path == NULL || path[0] == 0 || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid user-data root");

	root = calloc(1, sizeof(*root));

	if (root == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening user-data root");

	root->source = source;
	length = strlen(path) + 1;
	root->path = malloc(length);

	if (root->path == NULL) {
		free(root);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory storing user-data root");
	}

	memcpy(root->path, path, length);
	status = mh_user_data_init(&root->data, &operations, root, error_out);

	if (status == MH_ERROR_NONE)
		*output = &root->data;
	else {
		free(root->path);
		free(root);
	}

	return status;
}

void
mh_user_root_close(const struct mh_user_data *data)
{
	struct user_root *root;

	if (data == NULL)
		return;

	root = data->context;
	free(root->path);
	free(root);
}

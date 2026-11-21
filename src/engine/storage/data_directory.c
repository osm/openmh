#include <stdlib.h>
#include <string.h>

#include "engine/storage/data_directory_internal.h"
#include "interfaces/storage/data_stream_backend.h"

int
mh_data_name_valid(const char *name)
{
	return name != NULL && name[0] != 0 && strcmp(name, ".") != 0 &&
		strcmp(name, "..") != 0 && strpbrk(name, "/\\:") == NULL;
}

enum mh_error_code
mh_data_directory_bind(const struct mh_data_directory_ops *ops, void *context,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct mh_data_directory *directory;

	if (ops == NULL || output == NULL || ops->validate == NULL ||
		ops->list == NULL || ops->read == NULL ||
		ops->file_close == NULL || ops->close == NULL ||
		ops->file_validate == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory provider");

	if (ops->streams != NULL &&
		(ops->streams->open == NULL || ops->streams->append == NULL ||
			ops->streams->write_at == NULL ||
			ops->streams->seal == NULL ||
			ops->streams->create == NULL ||
			ops->streams->replace == NULL ||
			ops->streams->close == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete private stream provider");

	if (ops->browse != NULL &&
		(ops->browse->path == NULL || ops->browse->same == NULL ||
			ops->browse->inspect == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete directory inspection provider");

	directory = malloc(sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening writable directory");

	directory->ops = ops;
	directory->context = context;
	*output = directory;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_directory_validate(
	struct mh_data_directory *directory, struct mh_error *error_out)
{
	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing writable directory");

	return directory->ops->validate(directory->context, error_out);
}

enum mh_error_code
mh_data_directory_list(struct mh_data_directory *directory,
	mh_directory_visit visit, void *context, struct mh_error *error_out)
{
	if (directory == NULL || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory listing");

	return directory->ops->list(
		directory->context, visit, context, error_out);
}

enum mh_error_code
mh_data_directory_open_file(struct mh_data_directory *directory,
	const char *name, struct mh_data_file **output,
	struct mh_error *error_out)
{
	struct mh_data_file *file;
	enum mh_error_code status;

	if (directory == NULL || !mh_data_name_valid(name) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid stored file open");

	if (directory->ops->open_file == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"stored file identity capture is unavailable");

	file = malloc(sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	status = directory->ops->open_file(
		directory->context, name, &file->token, error_out);

	if (status != MH_ERROR_NONE) {
		free(file);
		return status;
	}

	file->directory = directory;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_directory_read(struct mh_data_directory *directory, const char *name,
	size_t limit, struct mh_file *bytes, struct mh_data_file **output,
	struct mh_error *error_out)
{
	struct mh_data_file *file;
	enum mh_error_code status;

	if (directory == NULL || !mh_data_name_valid(name) || bytes == NULL ||
		output == NULL || limit == (size_t)-1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory read");

	file = malloc(sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	status = directory->ops->read(directory->context, name, limit, bytes,
		&file->token, error_out);

	if (status != MH_ERROR_NONE) {
		free(file);
		return status;
	}

	file->directory = directory;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_directory_create(struct mh_data_directory *directory, const char *name,
	const void *bytes, size_t size, mh_data_publish_check check,
	void *context, struct mh_data_file **output, struct mh_error *error_out)
{
	struct mh_data_file *file;
	enum mh_error_code status;

	if (directory == NULL || !mh_data_name_valid(name) || output == NULL ||
		(bytes == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory creation");

	if (directory->ops->create == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory creation is unavailable");

	file = malloc(sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating stored file");

	status = directory->ops->create(directory->context, name,
		bytes == NULL ? "" : bytes, size, check, context, directory,
		&file->token, error_out);

	if (status != MH_ERROR_NONE) {
		free(file);
		return status;
	}

	file->directory = directory;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_file_validate(struct mh_data_file *file, struct mh_error *error_out)
{
	if (file == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing stored file");

	return file->directory->ops->file_validate(
		file->directory->context, file->token, error_out);
}

enum mh_error_code
mh_data_file_replace(struct mh_data_file *file, const void *bytes, size_t size,
	mh_data_publish_check check, void *context, struct mh_error *error_out)
{
	if (file == NULL || (bytes == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid stored file replacement");

	if (file->directory->ops->replace == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"stored file replacement is unavailable");

	return file->directory->ops->replace(file->directory->context,
		file->token, bytes == NULL ? "" : bytes, size, check, context,
		file->directory, error_out);
}

enum mh_error_code
mh_data_file_remove(struct mh_data_file *file, struct mh_error *error_out)
{
	if (file == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing stored file");

	if (file->directory->ops->remove == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"stored file deletion is unavailable");

	return file->directory->ops->remove(
		file->directory->context, file->token, error_out);
}

void
mh_data_file_close(struct mh_data_file *file)
{
	if (file == NULL)
		return;

	file->directory->ops->file_close(file->directory->context, file->token);
	free(file);
}

void
mh_data_directory_close(struct mh_data_directory *directory)
{
	if (directory == NULL)
		return;

	directory->ops->close(directory->context);
	free(directory);
}

enum mh_error_code
mh_data_directory_protect(struct mh_data_directory *directory,
	const char *resource_root, struct mh_error *error_out)
{
	if (directory == NULL || resource_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid directory protection arguments");

	if (directory->ops->protect == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory protection updates are unavailable");

	return directory->ops->protect(
		directory->context, resource_root, error_out);
}

enum mh_error_code
mh_data_file_refresh(struct mh_data_file *file, struct mh_error *error_out)
{
	if (file == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing stored file");

	if (file->directory->ops->file_refresh == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"stored file identity refresh is unavailable");

	return file->directory->ops->file_refresh(
		file->directory->context, file->token, error_out);
}

const char *
mh_data_directory_path(const struct mh_data_directory *directory)
{
	if (directory == NULL || directory->ops->browse == NULL)
		return NULL;

	return directory->ops->browse->path(directory->context);
}

enum mh_error_code
mh_data_directory_same(struct mh_data_directory *directory,
	struct mh_data_directory *other, int *same, struct mh_error *error_out)
{
	if (directory == NULL || other == NULL || same == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid directory comparison");

	if (directory->ops->browse == NULL || other->ops->browse == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory comparison is unavailable");

	if (directory->ops->browse != other->ops->browse) {
		*same = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return directory->ops->browse->same(
		directory->context, other->context, same, error_out);
}

enum mh_error_code
mh_data_directory_inspect(struct mh_data_directory *directory, const char *name,
	struct mh_data_file **output, int *removable,
	struct mh_error *error_out)
{
	struct mh_data_file *file;
	enum mh_error_code status;
	int can_remove;

	if (directory == NULL || !mh_data_name_valid(name) || output == NULL ||
		removable == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid directory entry inspection");

	if (directory->ops->browse == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory inspection is unavailable");

	file = malloc(sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory inspecting directory entry");

	file->directory = directory;
	status = directory->ops->browse->inspect(
		directory->context, name, &file->token, &can_remove, error_out);

	if (status != MH_ERROR_NONE) {
		free(file);
		return status;
	}

	if (file->token == NULL) {
		free(file);
		file = NULL;
		can_remove = 0;
	}

	*output = file;
	*removable = can_remove;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_file_remove_entry(struct mh_data_file *file, struct mh_error *error_out)
{
	if (file == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing directory entry");

	if (file->directory->ops->browse == NULL ||
		file->directory->ops->browse->remove_entry == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory entry removal is unavailable");

	return file->directory->ops->browse->remove_entry(
		file->directory->context, file->token, error_out);
}

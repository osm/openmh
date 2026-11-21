#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data_internal.h"
#include "interfaces/storage/data_directory_backend.h"
#include "interfaces/storage/data_stream_backend.h"

struct memory_directory {
	struct memory_data	*memory;
	char			*path;
};

struct memory_file {
	char		*key;
	unsigned long	generation;
};

struct memory_stream {
	unsigned char	*bytes;
	size_t		size, capacity;
};

static void
close_stored_file(void *context, void *token)
{
	struct memory_file *file;

	(void)context;
	file = token;
	free(file->key);
	free(file);
}

static struct memory_file *
new_stored_file(const struct memory_directory *directory, const char *name)
{
	struct memory_file *file;
	size_t directory_length, name_length;

	directory_length = strlen(directory->path);
	name_length = strlen(name);

	if (directory_length > (size_t)-1 - 2 ||
		name_length > (size_t)-1 - directory_length - 2)
		return NULL;

	file = calloc(1, sizeof(*file));

	if (file == NULL)
		return NULL;

	file->key = malloc(directory_length + name_length + 2);

	if (file->key == NULL) {
		free(file);
		return NULL;
	}

	memcpy(file->key, directory->path, directory_length);

	if (directory_length != 0)
		file->key[directory_length++] = '/';

	memcpy(file->key + directory_length, name, name_length + 1);

	return file;
}

static enum mh_error_code
validate_stored_directory(void *context, struct mh_error *error_out)
{
	/* Memory directories are pinned until close and cannot be renamed. */
	(void)context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
list_stored_directory(void *context, mh_directory_visit visit, void *visitor,
	struct mh_error *error_out)
{
	struct memory_directory *directory;

	directory = context;

	return mh_user_data_list(&directory->memory->data, directory->path,
		visit, visitor, error_out);
}

static enum mh_error_code
open_stored_file(void *context, const char *name, void **output,
	struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry;

	directory = context;
	file = new_stored_file(directory, name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	entry = mh_memory_data_find(directory->memory, file->key);

	if (entry == NULL || entry->directory) {
		close_stored_file(NULL, file);
		return mh_error_set(error_out,
			entry == NULL ? MH_ERROR_NOT_FOUND : MH_ERROR_INVALID,
			entry == NULL ? "stored file is missing"
				: "stored file is not regular");
	}

	file->generation = entry->generation;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_stored_file(void *context, const char *name, size_t limit,
	struct mh_file *bytes, void **output, struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	enum mh_error_code status;

	directory = context;
	file = new_stored_file(directory, name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	status = mh_user_data_read(
		&directory->memory->data, file->key, limit, bytes, error_out);

	if (status != MH_ERROR_NONE) {
		close_stored_file(NULL, file);
		return status;
	}

	file->generation =
		mh_memory_data_find(directory->memory, file->key)->generation;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_stored_file(void *context, const char *name, const void *bytes,
	size_t size, mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, void **output,
	struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry;
	enum mh_error_code status;

	directory = context;
	file = new_stored_file(directory, name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating stored file");

	if (mh_memory_data_find(directory->memory, file->key) != NULL ||
		mh_memory_data_conflicts(directory->memory, file->key, 0)) {
		close_stored_file(NULL, file);
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"stored file destination already exists");
	}

	if (directory->memory->generation == ULONG_MAX) {
		close_stored_file(NULL, file);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"user-data revision limit reached");
	}

	entry = calloc(1, sizeof(*entry));

	if (entry != NULL) {
		entry->key = malloc(strlen(file->key) + 1);
		entry->bytes = malloc(size == 0 ? 1 : size);
	}

	status = MH_ERROR_NONE;

	if (entry == NULL || entry->key == NULL || entry->bytes == NULL)
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating stored file");
	else {
		strcpy(entry->key, file->key);
		memcpy(entry->bytes, bytes, size);
		entry->size = size;

		if (check != NULL)
			status = check(checker, owner, error_out);
	}

	if (status != MH_ERROR_NONE) {
		if (entry != NULL) {
			free(entry->key);
			free(entry->bytes);
			free(entry);
		}

		close_stored_file(NULL, file);

		return status;
	}

	entry->generation = ++directory->memory->generation;
	file->generation = entry->generation;
	mh_memory_data_insert(directory->memory, entry);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_stored_file(void *context, void *token, struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry;

	directory = context;
	file = token;
	entry = mh_memory_data_find(directory->memory, file->key);

	if (entry == NULL || entry->directory ||
		entry->generation != file->generation)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"stored file changed since loading");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_stored_file(void *context, void *token, const void *bytes, size_t size,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry;
	unsigned char *copy;
	enum mh_error_code status;

	directory = context;
	file = token;
	status = validate_stored_file(directory, file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (directory->memory->generation == ULONG_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"user-data revision limit reached");

	copy = malloc(size == 0 ? 1 : size);

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory replacing stored file");

	memcpy(copy, bytes, size);

	if (check != NULL)
		status = check(checker, owner, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_stored_file(directory, file, error_out);

	if (status != MH_ERROR_NONE) {
		free(copy);
		return status;
	}

	entry = mh_memory_data_find(directory->memory, file->key);
	free(entry->bytes);
	entry->bytes = copy;
	entry->size = size;
	entry->generation = ++directory->memory->generation;
	file->generation = entry->generation;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
remove_stored_file(void *context, void *token, struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry, **link;

	directory = context;
	file = token;
	link = &directory->memory->entries;

	while (*link != NULL && strcmp((*link)->key, file->key) != 0)
		link = &(*link)->next;

	entry = *link;

	if (entry == NULL || entry->directory ||
		entry->generation != file->generation)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"stored file changed since loading; reload before deletion");

	*link = entry->next;
	free(entry->key);
	free(entry->bytes);
	free(entry);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_stored_directory(void *context)
{
	struct memory_directory *directory;

	directory = context;
	free(directory->path);
	free(directory);
}

static enum mh_directory_action
found_directory(void *context, const char *name)
{
	(void)context;
	(void)name;

	return MH_DIRECTORY_STOP;
}

#define STREAM_INITIAL_CAPACITY 4096

static enum mh_error_code
open_stream(void *context, void **output, struct mh_error *error_out)
{
	struct memory_stream *stream;

	(void)context;
	stream = calloc(1, sizeof(*stream));

	if (stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening private stream");

	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_stream(void *context, void *token, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct memory_stream *stream;
	unsigned char *next;
	size_t capacity, required;

	(void)context;
	stream = token;

	/* The public dispatcher checks the sum before entering the provider. */
	required = stream->size + size;

	if (required > stream->capacity) {
		capacity = stream->capacity == 0 ? STREAM_INITIAL_CAPACITY
						: stream->capacity;

		while (capacity < required) {
			if (capacity > (size_t)-1 / 2) {
				capacity = required;
				break;
			}

			capacity *= 2;
		}

		next = realloc(stream->bytes, capacity);

		if (next == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory appending private stream");

		stream->bytes = next;
		stream->capacity = capacity;
	}

	if (size != 0)
		memcpy(stream->bytes + stream->size, bytes, size);

	stream->size = required;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
write_stream_at(void *context, void *token, size_t offset, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	struct memory_stream *stream;

	(void)context;
	stream = token;

	if (size != 0)
		memcpy(stream->bytes + offset, bytes, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
seal_stream(void *context, void *token, size_t size, struct mh_error *error_out)
{
	struct memory_stream *stream;

	(void)context;
	stream = token;

	if (stream->size != size)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stream length changed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_stream(void *context, void *token, const char *name,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *directory, void **output,
	struct mh_error *error_out)
{
	struct memory_stream *stream;

	stream = token;

	return create_stored_file(context, name,
		stream->bytes == NULL ? (const void *)"" : stream->bytes,
		stream->size, check, checker, directory, output, error_out);
}

static enum mh_error_code
replace_stream(void *context, void *token, void *file,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *directory, struct mh_error *error_out)
{
	struct memory_stream *stream;

	stream = token;

	return replace_stored_file(context, file,
		stream->bytes == NULL ? (const void *)"" : stream->bytes,
		stream->size, check, checker, directory, error_out);
}

static void
close_stream(void *context, void *token)
{
	struct memory_stream *stream;

	(void)context;
	stream = token;
	free(stream->bytes);
	free(stream);
}

static enum mh_error_code
protect_directory(
	void *context, const char *resource_root, struct mh_error *error_out)
{
	(void)context;

	(void)resource_root;

	/* Packaged resources cannot alias this provider's private namespace. */

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const char *
directory_path(void *context)
{
	struct memory_directory *directory;

	directory = context;

	return directory->path;
}

static enum mh_error_code
same_directory(
	void *context, void *other, int *same, struct mh_error *error_out)
{
	struct memory_directory *directory, *other_directory;

	directory = context;
	other_directory = other;
	*same = directory->memory == other_directory->memory &&
		strcmp(directory->path, other_directory->path) == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
inspect_entry(void *context, const char *name, void **output, int *removable,
	struct mh_error *error_out)
{
	struct memory_directory *directory;
	struct memory_file *file;
	struct memory_entry *entry;
	enum mh_error_code status;

	directory = context;
	file = new_stored_file(directory, name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory inspecting directory entry");

	entry = mh_memory_data_find(directory->memory, file->key);

	if (entry == NULL) {
		/* Listing also exposes implicit parent directories. */
		status = mh_user_data_check_directory(
			&directory->memory->data, file->key, error_out);
		close_stored_file(NULL, file);

		if (status != MH_ERROR_NONE)
			return status;

		*output = NULL;
		*removable = 0;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (entry->directory) {
		close_stored_file(NULL, file);
		file = NULL;
	} else
		file->generation = entry->generation;

	*output = file;
	*removable = file != NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_directory(const struct mh_user_data *data, const char *path, int pin,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	static const struct mh_data_stream_ops streams = {open_stream,
		append_stream, write_stream_at, seal_stream, create_stream,
		replace_stream, close_stream};

	static const struct mh_data_browse_ops browse = {directory_path,
		same_directory, inspect_entry, remove_stored_file};

	static const struct mh_data_directory_ops ops = {
		validate_stored_directory, list_stored_directory,
		read_stored_file, create_stored_file, remove_stored_file,
		close_stored_file, close_stored_directory, validate_stored_file,
		replace_stored_file, open_stored_file, &streams,
		protect_directory, validate_stored_file, &browse};

	struct memory_directory *directory;
	enum mh_error_code status;
	size_t length;

	if (mh_memory_data_context(data) == NULL || path == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid memory writable directory");

	status =
		mh_user_data_list(data, path, found_directory, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	directory = calloc(1, sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening writable directory");

	directory->path = malloc(strlen(path) + 1);

	if (directory->path == NULL) {
		free(directory);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening writable directory");
	}

	strcpy(directory->path, path);
	length = strlen(path);

	while (length != 0 && directory->path[length - 1] == '/')
		directory->path[--length] = 0;

	directory->memory = data->context;

	/* Retain empty implicit parents after their last child is removed. */
	if (pin)
		status = mh_user_data_create_directory(
			data, directory->path, error_out);
	else
		status = MH_ERROR_NONE;

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_bind(
			&ops, directory, output, error_out);

	if (status != MH_ERROR_NONE)
		close_stored_directory(directory);

	return status;
}

enum mh_error_code
mh_memory_data_directory_open(const struct mh_user_data *data, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	return open_directory(data, path, 1, output, error_out);
}

enum mh_error_code
mh_memory_data_directory_browse(const struct mh_user_data *data,
	const char *path, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	return open_directory(data, path, 0, output, error_out);
}

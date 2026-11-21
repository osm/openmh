#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data_internal.h"
#include "interfaces/storage/data_directory_backend.h"

struct memory_entry *
mh_memory_data_find(const struct memory_data *memory, const char *key)
{
	struct memory_entry *entry;

	for (entry = memory->entries; entry != NULL; entry = entry->next)
		if (strcmp(entry->key, key) == 0)
			return entry;

	return NULL;
}

void
mh_memory_data_insert(struct memory_data *memory, struct memory_entry *entry)
{
	struct memory_entry **link;

	link = &memory->entries;

	while (*link != NULL && strcmp((*link)->key, entry->key) < 0)
		link = &(*link)->next;

	entry->next = *link;
	*link = entry;
}

static int
valid_key(const char *key)
{
	size_t length;

	length = strlen(key);

	return length != 0 && key[0] != '/' && key[length - 1] != '/' &&
		strstr(key, "//") == NULL;
}

static int
child_of(const char *key, const char *parent, size_t length)
{
	return strncmp(key, parent, length) == 0 && key[length] == '/';
}

int
mh_memory_data_conflicts(
	const struct memory_data *memory, const char *key, int directory)
{
	const struct memory_entry *entry;
	size_t length;

	length = strlen(key);

	for (entry = memory->entries; entry != NULL; entry = entry->next) {
		if (strcmp(entry->key, key) == 0) {
			if (entry->directory != directory)
				return 1;
		} else if ((!directory && child_of(entry->key, key, length)) ||
			(!entry->directory &&
				child_of(key, entry->key, strlen(entry->key))))
			return 1;
	}

	return 0;
}

static enum mh_error_code
read_data(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct memory_entry *entry;
	unsigned char *bytes;

	entry = mh_memory_data_find(context, key);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"user-data key not found");

	if (entry->directory)
		return mh_error_set(
			error_out, MH_ERROR_IO, "user-data key is a directory");

	if (entry->size > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"user data exceeds read limit");

	bytes = malloc(entry->size == 0 ? 1 : entry->size);

	if (bytes == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading user data");

	if (entry->size != 0)
		memcpy(bytes, entry->bytes, entry->size);

	output->data = bytes;
	output->size = entry->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_data(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct memory_data *memory;
	struct memory_entry *entry;
	unsigned char *copy;

	memory = context;

	if (!valid_key(key))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid memory user-data key");

	if (mh_memory_data_conflicts(memory, key, 0))
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"user-data key conflicts with a directory or parent file");

	if (memory->generation == ULONG_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"user-data revision limit reached");

	copy = malloc(size == 0 ? 1 : size);

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory replacing user data");

	if (size != 0)
		memcpy(copy, bytes, size);

	entry = mh_memory_data_find(memory, key);

	if (entry == NULL) {
		entry = calloc(1, sizeof(*entry));

		if (entry != NULL)
			entry->key = malloc(strlen(key) + 1);

		if (entry == NULL || entry->key == NULL) {
			free(entry);
			free(copy);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory creating user-data key");
		}

		strcpy(entry->key, key);
		mh_memory_data_insert(memory, entry);
	}

	free(entry->bytes);
	entry->bytes = copy;
	entry->size = size;
	entry->generation = ++memory->generation;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
default_directory(void *context, char **output, struct mh_error *error_out)
{
	char *root;

	(void)context;
	root = malloc(1);

	if (root == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resolving user-data directory");

	root[0] = 0;
	*output = root;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_directory(void *context, const char *path, struct mh_error *error_out)
{
	struct memory_data *memory;
	struct memory_entry *entry;
	char *key;
	size_t length;

	memory = context;

	if (path[0] == '/')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"memory user-data paths must be relative");

	length = strlen(path);

	while (length != 0 && path[length - 1] == '/')
		length--;

	if (length == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	key = malloc(length + 1);

	if (key == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating user-data directory");

	memcpy(key, path, length);
	key[length] = 0;

	if (!valid_key(key)) {
		free(key);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid memory user-data directory");
	}

	if (mh_memory_data_conflicts(memory, key, 1)) {
		free(key);
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"user-data directory conflicts with a file");
	}

	if (mh_memory_data_find(memory, key) != NULL) {
		free(key);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	entry = calloc(1, sizeof(*entry));

	if (entry == NULL) {
		free(key);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating user-data directory");
	}

	entry->key = key;
	entry->directory = 1;
	mh_memory_data_insert(memory, entry);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * A sibling such as "Controls-old" can sort between a directory and its files.
 */
static int
listed_before(const struct memory_data *memory,
	const struct memory_entry *current, size_t length)
{
	const struct memory_entry *entry;

	for (entry = memory->entries; entry != current; entry = entry->next)
		if (strncmp(entry->key, current->key, length) == 0 &&
			(entry->key[length] == 0 || entry->key[length] == '/'))
			return 1;

	return 0;
}

static enum mh_error_code
list_data(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	const struct memory_data *memory;
	const struct memory_entry *entry;
	const char *child, *end;
	char *name;
	size_t length, size;
	enum mh_directory_action action;
	enum mh_error_code status;
	int found;

	memory = context;

	if (path[0] == '/')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"memory user-data paths must be relative");

	length = strlen(path);

	while (length != 0 && path[length - 1] == '/')
		length--;

	found = length == 0;
	status = MH_ERROR_NONE;

	for (entry = memory->entries; entry != NULL; entry = entry->next) {
		if (strlen(entry->key) == length &&
			strncmp(entry->key, path, length) == 0) {
			if (!entry->directory) {
				status = mh_error_set(error_out, MH_ERROR_IO,
					"user-data key is not a directory");
				break;
			}

			found = 1;
			continue;
		}

		if (length != 0 && !child_of(entry->key, path, length))
			continue;

		child = entry->key + (length == 0 ? 0 : length + 1);
		found = 1;
		end = strchr(child, '/');
		size = end == NULL ? strlen(child) : (size_t)(end - child);

		if (listed_before(
			memory, entry, (size_t)(child - entry->key) + size))
			continue;

		name = malloc(size + 1);

		if (name == NULL) {
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory listing user data");
			break;
		}

		memcpy(name, child, size);
		name[size] = 0;
		action = visit(visitor, name);
		free(name);

		if (action == MH_DIRECTORY_STOP)
			break;

		if (action != MH_DIRECTORY_CONTINUE) {
			status = mh_error_set(error_out, MH_ERROR_IO,
				"user-data enumeration callback failed");
			break;
		}
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"user-data directory not found");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct memory_data *memory;

	(void)resource_root;
	memory = context;

	return mh_memory_data_directory_open(
		&memory->data, path, output, error_out);
}

static enum mh_error_code
browse_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct memory_data *memory;

	(void)resource_root;
	memory = context;

	return mh_memory_data_directory_browse(
		&memory->data, path, output, error_out);
}

static enum mh_error_code
check_directory(void *context, const char *path, struct mh_error *error_out)
{
	const struct memory_data *memory;
	const struct memory_entry *entry;
	size_t length;

	memory = context;

	if (path[0] == '/')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"memory user-data paths must be relative");

	length = strlen(path);

	while (length != 0 && path[length - 1] == '/')
		length--;

	if (length == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (entry = memory->entries; entry != NULL; entry = entry->next) {
		if (strlen(entry->key) == length &&
			strncmp(entry->key, path, length) == 0) {
			if (!entry->directory)
				return mh_error_set(error_out, MH_ERROR_IO,
					"user-data key is not a directory");

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (child_of(entry->key, path, length))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_NOT_FOUND, "user-data directory not found");
}

enum mh_error_code
mh_memory_user_data_open(
	const struct mh_user_data **output, struct mh_error *error_out)
{
	static const struct mh_user_data_ops ops = {read_data, replace_data,
		default_directory, list_data, create_directory, open_directory,
		check_directory, browse_directory};

	struct memory_data *memory;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing user-data output");

	memory = calloc(1, sizeof(*memory));

	if (memory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening user data");

	(void)mh_user_data_init(&memory->data, &ops, memory, NULL);
	*output = &memory->data;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_memory_user_data_close(const struct mh_user_data *data)
{
	struct memory_data *memory;
	struct memory_entry *entry, *next;

	if (data == NULL)
		return;

	memory = data->context;

	for (entry = memory->entries; entry != NULL; entry = next) {
		next = entry->next;
		free(entry->key);
		free(entry->bytes);
		free(entry);
	}

	free(memory);
}

struct memory_data *
mh_memory_data_context(const struct mh_user_data *data)
{
	return data != NULL && data->ops->read == read_data ? data->context
							: NULL;
}

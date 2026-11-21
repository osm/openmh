#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/storage/resource_directory.h"

struct memory_resources {
	struct mh_resource_source	source;
	struct mh_resource_entry	*entries;
	size_t				count;
};

static int
compare_entries(const void *left, const void *right)
{
	const struct mh_resource_entry *first, *second;

	first = left;
	second = right;

	return strcmp(first->path, second->path);
}

static const struct mh_resource_entry *
find_entry(const struct memory_resources *memory, const char *path)
{
	struct mh_resource_entry key;

	if (memory->count == 0)
		return NULL;

	key.path = path;

	return bsearch(&key, memory->entries, memory->count,
		sizeof(*memory->entries), compare_entries);
}

static enum mh_error_code
read_range(void *context, const char *path, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	const struct mh_resource_entry *entry;

	entry = find_entry(context, path);

	if (entry == NULL)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "resource not found");

	if (offset > entry->size || size > entry->size - (size_t)offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource range outside available bytes");

	if (size != 0)
		memcpy(output, entry->data + (size_t)offset, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	const struct mh_resource_entry *entry;
	unsigned char *data;

	entry = find_entry(context, path);

	if (entry == NULL)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "resource not found");

	if (entry->size > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource exceeds read limit");

	data = malloc(entry->size == 0 ? 1 : entry->size);

	if (data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading resource");

	if (entry->size != 0)
		memcpy(data, entry->data, entry->size);

	output->data = data;
	output->size = entry->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resource_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	const struct mh_resource_entry *entry;

	entry = find_entry(context, path);

	if (entry == NULL)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "resource not found");

	if (entry->size > ULONG_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource size exceeds native range");

	*output = (unsigned long)entry->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const char *
child_path(const char *key, const char *path, size_t length)
{
	if (length == 0)
		return key;

	return strncmp(key, path, length) == 0 && key[length] == '/'
		? key + length + 1
		: NULL;
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	const struct memory_resources *memory;
	const char *child, *end;
	char *name, *previous;
	size_t i, length, size;
	enum mh_directory_action action;
	enum mh_error_code status;
	int found;

	memory = context;
	length = strlen(path);
	previous = NULL;
	found = length == 0;
	status = MH_ERROR_NONE;

	for (i = 0; i < memory->count; i++) {
		child = child_path(memory->entries[i].path, path, length);

		if (child == NULL)
			continue;

		found = 1;
		end = strchr(child, '/');
		size = end == NULL ? strlen(child) : (size_t)(end - child);

		if (previous != NULL && strlen(previous) == size &&
			strncmp(previous, child, size) == 0)
			continue;

		name = malloc(size + 1);

		if (name == NULL) {
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory listing resources");
			break;
		}

		memcpy(name, child, size);
		name[size] = 0;
		free(previous);
		previous = name;
		action = visit(visitor, name);

		if (action == MH_DIRECTORY_STOP)
			break;

		if (action == MH_DIRECTORY_FAIL) {
			status = mh_error_set(error_out, MH_ERROR_IO,
				"resource enumeration callback failed");
			break;
		}
	}

	free(previous);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"resource directory not found");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_directory(void *context, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct memory_resources *memory;

	memory = context;

	return mh_resource_directory_open(
		&memory->source, path, output, error_out);
}

enum mh_error_code
mh_memory_resources_open(const struct mh_resource_entry *entries, size_t count,
	const struct mh_resource_source **output, struct mh_error *error_out)
{
	static const struct mh_resource_ops ops = {read_resource, resource_size,
		list_resources, read_range, open_directory};

	struct memory_resources *memory;
	size_t i, length;

	if (output == NULL || (count != 0 && entries == NULL) ||
		count > (size_t)-1 / sizeof(*entries))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid memory resources");

	for (i = 0; i < count; i++) {
		if (entries[i].path == NULL || entries[i].path[0] == 0 ||
			(entries[i].size != 0 && entries[i].data == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid memory resource entry");

		length = strlen(entries[i].path);

		if (entries[i].path[0] == '/' ||
			entries[i].path[length - 1] == '/' ||
			strstr(entries[i].path, "//") != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid memory resource key");
	}

	memory = calloc(1, sizeof(*memory));

	if (memory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening resources");

	if (count != 0) {
		memory->entries = malloc(count * sizeof(*entries));

		if (memory->entries == NULL) {
			free(memory);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory indexing resources");
		}

		memcpy(memory->entries, entries, count * sizeof(*entries));
		qsort(memory->entries, count, sizeof(*entries),
			compare_entries);
	}

	for (i = 1; i < count; i++) {
		if (strcmp(memory->entries[i - 1].path,
			memory->entries[i].path) == 0) {
			free(memory->entries);
			free(memory);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate memory resource key");
		}
	}

	memory->count = count;
	(void)mh_resource_source_init(&memory->source, &ops, memory, NULL);
	*output = &memory->source;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_memory_resources_close(const struct mh_resource_source *source)
{
	struct memory_resources *memory;

	if (source == NULL)
		return;

	memory = source->context;
	free(memory->entries);
	free(memory);
}

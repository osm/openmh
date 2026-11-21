#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/storage/zip_internal.h"
#include "backends/storage/zip_provider.h"
#include "engine/core/text.h"
#include "engine/storage/resource_directory.h"

static const char *
relative_path(const struct mh_zip_resources *zip, const char *path)
{
	size_t length;

	length = strlen(zip->path);

	if (strncmp(path, zip->path, length) != 0)
		return NULL;

	if (path[length] == 0)
		return path + length;

	return path[length] == '/' ? path + length + 1 : NULL;
}

static const struct mh_zip_entry *
find_entry(const struct mh_zip_resources *zip, const char *path)
{
	struct mh_zip_entry key;
	const struct mh_zip_entry *entry;
	const char *relative;

	relative = relative_path(zip, path);

	if (relative == NULL)
		return NULL;

	key.path = (char *)relative;
	entry = bsearch(&key, zip->entries, zip->count, sizeof(*zip->entries),
		mh_zip_compare_entries);

	return entry != NULL && !entry->directory ? entry : NULL;
}

static enum mh_error_code
resource_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	const struct mh_zip_entry *entry;

	entry = find_entry(context, path);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"package resource not found");

	*output = entry->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
clear_cache(struct mh_zip_resources *zip)
{
	free(zip->cached_file.data);
	zip->cached_file.data = NULL;
	zip->cached_file.size = 0;
	zip->cached_entry = NULL;
}

static enum mh_error_code
read_compressed_range(struct mh_zip_resources *zip,
	const struct mh_zip_entry *entry, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (zip->cached_entry != entry) {
		clear_cache(zip);
		status = mh_zip_inflate(
			zip, entry, &zip->cached_file, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		zip->cached_entry = entry;
	}

	memcpy(output, zip->cached_file.data + offset, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_range(void *context, const char *path, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	struct mh_zip_resources *zip;
	const struct mh_zip_entry *entry;

	zip = context;
	entry = find_entry(zip, path);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"package resource not found");

	if (offset > entry->size || size > entry->size - offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"read outside package resource");

	if (entry->method == 8)
		return read_compressed_range(
			zip, entry, offset, output, size, error_out);

	return mh_resource_source_read_at(zip->source, zip->path,
		entry->offset + offset, output, size, error_out);
}

static enum mh_error_code
read_stored(const struct mh_zip_resources *zip,
	const struct mh_zip_entry *entry, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	size_t offset, chunk;
	unsigned long crc;
	enum mh_error_code status;

	file.size = (size_t)entry->size;
	file.data = malloc(file.size == 0 ? 1 : file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading package resource");

	status = mh_resource_source_read_at(zip->source, zip->path,
		entry->offset, file.data, file.size, error_out);
	crc = crc32(0, Z_NULL, 0);

	for (offset = 0; status == MH_ERROR_NONE && offset < file.size;
		offset += chunk) {
		chunk = file.size - offset;

		if (chunk > 65536U)
			chunk = 65536U;

		crc = crc32(crc, file.data + offset, (uInt)chunk);
	}

	if (status == MH_ERROR_NONE && crc != entry->crc)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"package resource CRC mismatch");

	if (status != MH_ERROR_NONE) {
		free(file.data);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_zip_resources *zip;
	const struct mh_zip_entry *entry;

	zip = context;
	entry = find_entry(zip, path);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"package resource not found");

	if (entry->size > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"package resource exceeds read limit");

	if (entry->method == 0)
		return read_stored(zip, entry, output, error_out);

	if (zip->cached_entry == entry) {
		/*
		 * Transfer the verified allocation instead of expanding it
		 * again.
		 */
		*output = zip->cached_file;
		zip->cached_file.data = NULL;
		clear_cache(zip);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	clear_cache(zip);

	return mh_zip_inflate(zip, entry, output, error_out);
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	const struct mh_zip_resources *zip;
	const char *relative, *entry, *child, *slash;
	char previous[MH_ZIP_PATH_MAX], name[MH_ZIP_PATH_MAX];
	size_t length, size, i;
	int found;
	enum mh_directory_action action;

	zip = context;
	relative = relative_path(zip, path);

	if (relative == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"package directory not found");

	length = strlen(relative);
	previous[0] = 0;
	found = length == 0;

	for (i = 0; i < zip->count; i++) {
		entry = zip->entries[i].path;

		if (length != 0) {
			if (!mh_ascii_equal_n(entry, relative, length))
				continue;

			if (entry[length] == 0 && zip->entries[i].directory)
				found = 1;

			if (entry[length] != '/')
				continue;
		}

		child = entry + (length == 0 ? 0 : length + 1);
		slash = strchr(child, '/');
		size = slash == NULL ? strlen(child) : (size_t)(slash - child);
		memcpy(name, child, size);
		name[size] = 0;
		found = 1;

		if (mh_ascii_equal(name, previous))
			continue;

		strcpy(previous, name);
		action = visit(visitor, name);

		if (action == MH_DIRECTORY_STOP)
			break;

		if (action == MH_DIRECTORY_FAIL)
			return mh_error_set(error_out, MH_ERROR_IO,
				"package enumeration callback failed");
	}

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"package directory not found");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_directory(void *context, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct mh_zip_resources *zip;

	zip = context;

	return mh_resource_directory_open(&zip->view, path, output, error_out);
}

static void
close_resources(const struct mh_resource_source *resources)
{
	struct mh_zip_resources *zip;

	if (resources == NULL)
		return;

	zip = resources->context;
	clear_cache(zip);
	mh_zip_index_free(zip);
	free(zip->path);
	free(zip);
}

static enum mh_error_code
open_resources(const struct mh_resource_source *source, const char *path,
	const struct mh_resource_source **output, struct mh_error *error_out)
{
	static const struct mh_resource_ops ops = {read_resource, resource_size,
		list_resources, read_range, open_directory};

	struct mh_zip_resources *zip;
	enum mh_error_code status;

	if (source == NULL || path == NULL || path[0] == 0 || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid package source");

	zip = calloc(1, sizeof(*zip));

	if (zip == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening package");

	zip->view.ops = &ops;
	zip->view.context = zip;
	zip->source = source;
	zip->path = malloc(strlen(path) + 1);

	if (zip->path == NULL) {
		close_resources(&zip->view);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening package");
	}

	strcpy(zip->path, path);
	status = mh_zip_index(zip, error_out);

	if (status != MH_ERROR_NONE) {
		close_resources(&zip->view);
		return status;
	}

	*output = &zip->view;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_resource_archive_provider *
mh_zip_resource_provider(void)
{
	static const struct mh_resource_archive_provider provider = {
		open_resources, close_resources};

	return &provider;
}

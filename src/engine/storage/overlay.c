#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/overlay.h"
#include "engine/storage/resource_directory.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/resource_backend.h"

struct overlay_entry {
	char				*name, *path;
	const struct mh_resource_source	*source;
	int				directory;
};

struct mh_resource_overlay {
	struct mh_resource_source	source;
	struct overlay_entry		*entries;
	size_t				count, capacity;
	char				*root;
};

struct overlay_scan {
	struct mh_resource_overlay	*overlay;
	const struct mh_resource_layer	*layer;
	const char			*relative;
	unsigned int			depth;
	struct mh_error			error;
};

static char *
copy_text(const char *text)
{
	char *copy;

	copy = malloc(strlen(text) + 1);

	if (copy != NULL)
		strcpy(copy, text);

	return copy;
}

static enum mh_error_code
join(const char *root, const char *name, char **output, struct mh_error *error)
{
	size_t a, b;
	char *path;

	a = strlen(root);
	b = strlen(name);

	if (a > 4096 || b > 4096 || a + b > 4096)
		return mh_error_set(
			error, MH_ERROR_RANGE, "asset path too long");

	path = malloc(a + b + 2);

	if (path == NULL)
		return mh_error_set(error, MH_ERROR_MEMORY,
			"out of memory indexing assets");

	memcpy(path, root, a);

	if (a != 0 && b != 0 && path[a - 1] != '/')
		path[a++] = '/';

	memcpy(path + a, name, b + 1);
	*output = path;

	return MH_ERROR_NONE;
}

static size_t
position(const struct mh_resource_overlay *overlay, const char *name)
{
	size_t low, high, mid;

	low = 0;
	high = overlay->count;

	while (low < high) {
		mid = low + (high - low) / 2;

		if (mh_ascii_compare(overlay->entries[mid].name, name) < 0)
			low = mid + 1;
		else
			high = mid;
	}

	return low;
}

static enum mh_error_code
insert(struct mh_resource_overlay *overlay,
	const struct mh_resource_layer *layer, const char *name,
	const char *path, int directory, struct mh_error *error)
{
	struct overlay_entry entry, *next;
	size_t i, capacity;

	i = position(overlay, name);

	if (i < overlay->count &&
		mh_ascii_equal(overlay->entries[i].name, name)) {
		if (directory && overlay->entries[i].directory)
			return MH_ERROR_NONE;

		if (directory != overlay->entries[i].directory ||
			!layer->replace)
			return mh_error_set(error, MH_ERROR_EXISTS,
				"conflicting package resources (only patches may replace base files)");

		entry.path = copy_text(path);

		if (entry.path == NULL)
			return mh_error_set(error, MH_ERROR_MEMORY,
				"out of memory indexing assets");

		free(overlay->entries[i].path);
		overlay->entries[i].path = entry.path;
		overlay->entries[i].source = layer->source;

		return MH_ERROR_NONE;
	}

	if (overlay->count == overlay->capacity) {
		capacity = overlay->capacity == 0 ? 128 : overlay->capacity * 2;

		if (capacity > 65536)
			return mh_error_set(error, MH_ERROR_RANGE,
				"too many package resources");

		next = realloc(overlay->entries, capacity * sizeof(*next));

		if (next == NULL)
			return mh_error_set(error, MH_ERROR_MEMORY,
				"out of memory indexing assets");

		overlay->entries = next;
		overlay->capacity = capacity;
	}

	entry.name = copy_text(name);
	entry.path = copy_text(path);
	entry.source = layer->source;
	entry.directory = directory;

	if (entry.name == NULL || entry.path == NULL) {
		free(entry.name);
		free(entry.path);
		return mh_error_set(error, MH_ERROR_MEMORY,
			"out of memory indexing assets");
	}

	memmove(overlay->entries + i + 1, overlay->entries + i,
		(overlay->count - i) * sizeof(entry));
	overlay->entries[i] = entry;
	overlay->count++;

	return MH_ERROR_NONE;
}

static enum mh_error_code	scan_tree(struct overlay_scan *scan,
	const char *relative);

static enum mh_directory_action
scan_child(void *context, const char *name)
{
	struct overlay_scan *scan;
	char *relative;
	enum mh_error_code status;

	scan = context;

	if (name[0] == 0 || strchr(name, '/') != NULL ||
		strchr(name, '\\') != NULL || strcmp(name, ".") == 0 ||
		strcmp(name, "..") == 0) {
		mh_error_set(&scan->error, MH_ERROR_INVALID,
			"invalid package child name");
		return MH_DIRECTORY_FAIL;
	}

	relative = NULL;
	status = join(scan->relative, name, &relative, &scan->error);

	if (status == MH_ERROR_NONE)
		status = scan_tree(scan, relative);

	free(relative);

	return status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
				: MH_DIRECTORY_FAIL;
}

static enum mh_error_code
scan_tree(struct overlay_scan *scan, const char *relative)
{
	struct overlay_scan child;
	struct mh_data_directory *directory;
	char *path;
	unsigned long size;
	int is_directory;
	enum mh_error_code status;

	if (scan->depth >= 64)
		return mh_error_set(&scan->error, MH_ERROR_RANGE,
			"package directory nesting exceeds limit");

	path = NULL;
	directory = NULL;
	status = join(scan->layer->root, relative, &path, &scan->error);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_size(
		scan->layer->source, path, &size, &scan->error);
	is_directory = status != MH_ERROR_NONE;

	if (is_directory) {
		status = mh_resource_source_open_directory(
			scan->layer->source, path, &directory, &scan->error);
		mh_data_directory_close(directory);
	}

	if (status == MH_ERROR_NONE)
		status = insert(scan->overlay, scan->layer, relative, path,
			is_directory, &scan->error);

	if (status == MH_ERROR_NONE && is_directory) {
		child = *scan;
		child.relative = relative;
		child.depth++;
		mh_error_set(&child.error, MH_ERROR_NONE, NULL);
		status = mh_resource_source_list(scan->layer->source, path,
			scan_child, &child, &scan->error);

		if (child.error.code != MH_ERROR_NONE)
			status = mh_error_forward(&scan->error, &child.error);
	}

	free(path);

	return status;
}

static struct overlay_entry *
lookup(struct mh_resource_overlay *overlay, const char *path)
{
	size_t length, i;

	length = strlen(overlay->root);

	if (strncmp(path, overlay->root, length) != 0 ||
		(path[length] != 0 && path[length] != '/'))
		return NULL;

	path += length;

	if (*path == '/')
		path++;

	i = position(overlay, path);

	return i < overlay->count &&
			mh_ascii_equal(overlay->entries[i].name, path)
		? &overlay->entries[i]
		: NULL;
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error)
{
	struct overlay_entry *entry;

	entry = lookup(context, path);

	if (entry == NULL || entry->directory)
		return mh_error_set(error, MH_ERROR_NOT_FOUND,
			"package resource not found");

	return mh_resource_source_read_bounded(
		entry->source, entry->path, limit, output, error);
}

static enum mh_error_code
size_resource(void *context, const char *path, unsigned long *output,
	struct mh_error *error)
{
	struct overlay_entry *entry;

	entry = lookup(context, path);

	if (entry == NULL || entry->directory)
		return mh_error_set(error, MH_ERROR_NOT_FOUND,
			"package resource not found");

	return mh_resource_source_size(
		entry->source, entry->path, output, error);
}

static enum mh_error_code
range_resource(void *context, const char *path, unsigned long offset,
	void *output, size_t size, struct mh_error *error)
{
	struct overlay_entry *entry;

	entry = lookup(context, path);

	if (entry == NULL || entry->directory)
		return mh_error_set(error, MH_ERROR_NOT_FOUND,
			"package resource not found");

	return mh_resource_source_read_at(
		entry->source, entry->path, offset, output, size, error);
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error)
{
	struct mh_resource_overlay *overlay;
	struct overlay_entry *entry;
	const char *name;
	size_t length, i;
	enum mh_directory_action action;

	overlay = context;
	entry = lookup(overlay, path);

	if (entry == NULL || !entry->directory)
		return mh_error_set(error, MH_ERROR_NOT_FOUND,
			"package directory not found");

	length = strlen(entry->name);

	for (i = 0; i < overlay->count; i++) {
		name = overlay->entries[i].name;

		if (length != 0) {
			if (strlen(name) <= length ||
				!mh_ascii_equal_n(name, entry->name, length) ||
				name[length] != '/')
				continue;

			name += length + 1;
		}

		if (*name == 0 || strchr(name, '/') != NULL)
			continue;

		action = visit(visitor, name);

		if (action == MH_DIRECTORY_STOP)
			break;

		if (action == MH_DIRECTORY_FAIL)
			return mh_error_set(error, MH_ERROR_IO,
				"package directory visitor failed");
	}

	return mh_error_set(error, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_directory(void *context, const char *path,
	struct mh_data_directory **output, struct mh_error *error)
{
	struct mh_resource_overlay *overlay;

	overlay = context;

	return mh_resource_directory_open(
		&overlay->source, path, output, error);
}

void
mh_resource_overlay_close(struct mh_resource_overlay *overlay)
{
	size_t i;

	if (overlay == NULL)
		return;

	for (i = 0; i < overlay->count; i++) {
		free(overlay->entries[i].name);
		free(overlay->entries[i].path);
	}

	free(overlay->entries);
	free(overlay->root);
	free(overlay);
}

const struct mh_resource_source *
mh_resource_overlay_source(const struct mh_resource_overlay *overlay)
{
	return &overlay->source;
}

enum mh_error_code
mh_resource_overlay_open(const struct mh_resource_layer *layers, size_t count,
	const char *root, struct mh_resource_overlay **output,
	struct mh_error *error)
{
	static const struct mh_resource_ops ops = {read_resource, size_resource,
		list_resources, range_resource, open_directory};

	struct mh_resource_overlay *overlay;
	struct overlay_scan scan;
	size_t i;
	enum mh_error_code status;

	if (layers == NULL || count == 0 || root == NULL || *root == 0 ||
		output == NULL)
		return mh_error_set(
			error, MH_ERROR_INVALID, "invalid resource overlay");

	overlay = calloc(1, sizeof(*overlay));

	if (overlay == NULL)
		return mh_error_set(error, MH_ERROR_MEMORY,
			"out of memory mounting resources");

	overlay->root = copy_text(root);
	status = overlay->root == NULL ? MH_ERROR_MEMORY : MH_ERROR_NONE;
	memset(&scan, 0, sizeof(scan));
	scan.overlay = overlay;

	for (i = 0; status == MH_ERROR_NONE && i < count; i++) {
		if (layers[i].source == NULL || layers[i].root == NULL ||
			*layers[i].root == 0) {
			status = MH_ERROR_INVALID;
			break;
		}

		scan.layer = &layers[i];
		status = scan_tree(&scan, "");
	}

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_init(
			&overlay->source, &ops, overlay, error);

	if (status != MH_ERROR_NONE) {
		if (scan.error.code != MH_ERROR_NONE)
			mh_error_forward(error, &scan.error);
		else
			mh_error_set(
				error, status, "cannot mount resource overlay");
		mh_resource_overlay_close(overlay);
		return status;
	}

	*output = overlay;

	return mh_error_set(error, MH_ERROR_NONE, NULL);
}

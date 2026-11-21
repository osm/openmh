#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "engine/archive/zip_write.h"
#include "engine/assets/packages.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_installation.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/path_names.h"
#include "extract/output/compact.h"
#include "extract/output/entry.h"
#include "extract/output/package.h"
#include "extract/path/join.h"

struct names {
	char	**values;
	size_t	count, capacity;
	int	failed;
};

struct pack_tree {
	const char			*root;
	struct mh_zip_writer		*writer;

	mh_extract_progress		progress;
	void				*context;
	size_t				completed, total;
	int				counting;
	struct mh_extract_options	options;
};

static enum mh_directory_action
collect_name(void *context, const char *name)
{
	struct names *names;
	char **next, *copy;
	size_t capacity;

	names = context;

	if (names->count == names->capacity) {
		capacity = names->capacity == 0 ? 32 : names->capacity * 2;

		if (capacity > 65536U) {
			names->failed = 1;
			return MH_DIRECTORY_FAIL;
		}

		next = realloc(names->values, capacity * sizeof(*next));

		if (next == NULL) {
			names->failed = 1;
			return MH_DIRECTORY_FAIL;
		}

		names->values = next;
		names->capacity = capacity;
	}

	copy = malloc(strlen(name) + 1);

	if (copy == NULL) {
		names->failed = 1;
		return MH_DIRECTORY_FAIL;
	}

	strcpy(copy, name);
	names->values[names->count++] = copy;

	return MH_DIRECTORY_CONTINUE;
}

static int
compare_names(const void *left, const void *right)
{
	const char *const *a, *const *b;

	a = left;
	b = right;

	return mh_ascii_compare(*a, *b);
}

static enum mh_error_code	pack_directory(struct pack_tree *tree,
	const char *relative, unsigned int depth, struct mh_error *error_out);

static enum mh_error_code
pack_child(struct pack_tree *tree, const char *relative, unsigned int depth,
	struct mh_error *error_out)
{
	char *path;
	unsigned long size;
	struct mh_data_directory *directory;
	enum mh_error_code status;
	int is_directory;

	path = NULL;
	directory = NULL;
	status = mh_extract_join(tree->root, relative, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_size(
		mh_filesystem_resources(), path, &size, error_out);
	is_directory = status != MH_ERROR_NONE;

	if (is_directory) {
		status = mh_resource_source_open_directory(
			mh_filesystem_resources(), path, &directory, error_out);
		mh_data_directory_close(directory);
	}

	if (status == MH_ERROR_NONE) {
		if (tree->counting) {
			if (tree->total == (size_t)-1)
				status = mh_error_set(error_out, MH_ERROR_RANGE,
					"too many package entries");
			else
				tree->total++;
		} else if (tree->options.compact && !is_directory)
			status = mh_extract_compact_add(tree->writer,
				mh_filesystem_resources(), path, relative,
				tree->progress, tree->context, error_out);
		else
			status = mh_extract_package_add(tree->writer,
				mh_filesystem_resources(), path, relative,
				is_directory, error_out);
	}

	free(path);

	if (status == MH_ERROR_NONE && tree->progress != NULL) {
		if (tree->counting)
			status = tree->progress(tree->context, MH_EXTRACT_POLL,
				NULL, 0, 0, error_out);
		else
			status = tree->progress(tree->context,
				MH_EXTRACT_WRITE_PACKAGE, relative,
				++tree->completed, tree->total, error_out);
	}

	if (status == MH_ERROR_NONE && is_directory)
		status = pack_directory(tree, relative, depth + 1, error_out);

	return status;
}

static enum mh_error_code
has_wave_source(const struct pack_tree *tree, const char *directory,
	const struct names *names, size_t selected, int *found,
	struct mh_error *error_out)
{
	const char *name;
	char *relative, *path;
	size_t length, i;
	unsigned long size;
	enum mh_error_code status;

	*found = 0;
	name = names->values[selected];
	length = strlen(name);

	if (!tree->options.compact ||
		!mh_ascii_equal(directory, MH_ASSET_MUSIC_DIR) || length <= 5 ||
		!mh_ascii_equal(name + length - 5, ".opus"))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < names->count; i++) {
		if (strlen(names->values[i]) != length - 1 ||
			!mh_ascii_equal_n(name, names->values[i], length - 5) ||
			!mh_ascii_equal(names->values[i] + length - 5, ".wav"))
			continue;

		relative = path = NULL;
		status = mh_extract_join(
			directory, names->values[i], &relative, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_extract_join(
				tree->root, relative, &path, error_out);

		if (status == MH_ERROR_NONE)
			*found = mh_resource_source_size(
					mh_filesystem_resources(), path, &size,
					NULL) == MH_ERROR_NONE;

		free(relative);
		free(path);

		return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
excluded_root(const struct mh_extract_options *options, const char *name)
{
	return mh_ascii_equal(name, MH_ASSET_ORIGINAL_DIR) ||
		mh_ascii_equal(name, MH_USER_DATA_DIR) ||
		(options->exclude_music &&
			mh_ascii_equal(name, MH_ASSET_MUSIC_DIR)) ||
		(options->exclude_movies &&
			mh_ascii_equal(name, MH_ASSET_MOVIES_DIR));
}

static enum mh_error_code
pack_directory(struct pack_tree *tree, const char *relative, unsigned int depth,
	struct mh_error *error_out)
{
	struct names names;
	char *path, *child;
	size_t i;
	int replaced;
	enum mh_error_code status;

	if (depth > 64)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"package directory nesting exceeds limit");

	memset(&names, 0, sizeof(names));
	path = NULL;
	status = mh_extract_join(tree->root, relative, &path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_list(mh_filesystem_resources(),
			path, collect_name, &names, error_out);

	free(path);

	if (names.failed)
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot collect package directory");

	if (status == MH_ERROR_NONE && names.count != 0)
		qsort(names.values, names.count, sizeof(*names.values),
			compare_names);

	for (i = 0; status == MH_ERROR_NONE && i < names.count; i++) {
		if (i != 0 &&
			mh_ascii_equal(names.values[i - 1], names.values[i])) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous package filenames");
			break;
		}

		if (depth == 0 &&
			excluded_root(&tree->options, names.values[i]))
			continue;

		status = has_wave_source(
			tree, relative, &names, i, &replaced, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (replaced)
			continue;

		child = NULL;
		status = mh_extract_join(
			relative, names.values[i], &child, error_out);

		if (status == MH_ERROR_NONE)
			status = pack_child(tree, child, depth, error_out);

		free(child);
	}

	for (i = 0; i < names.count; i++)
		free(names.values[i]);

	free(names.values);

	return status;
}

enum mh_error_code
mh_extract_package_parent(const char *destination, const char *source,
	struct mh_data_directory **directory, const char **name,
	struct mh_error *error_out)
{
	char *parent;
	size_t length;
	enum mh_error_code status;

	length = mh_path_directory_length(destination);

	*name = destination + length;

	if (**name == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing package filename");

	parent = malloc(length + 2);

	if (parent == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening package destination");

	if (length == 0)
		strcpy(parent, ".");
	else {
		memcpy(parent, destination, length);
		parent[length] = 0;
	}

	status = mh_filesystem_data_directory_open(
		parent, source, directory, error_out);
	free(parent);

	return status;
}

enum mh_error_code
mh_extract_package_write(const char *source, struct mh_data_stream *stream,
	const struct mh_extract_options *options, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	struct mh_content_package_info info;
	struct pack_tree tree;
	enum mh_error_code status;

	memset(&tree, 0, sizeof(tree));
	tree.root = source;
	tree.options = *options;
	tree.progress = progress;
	tree.context = context;
	status = mh_content_package_probe(
		mh_filesystem_resources(), source, &info, error_out);

	/*
	 * Count the same filtered tree before writing so progress has a total.
	 */
	tree.counting = 1;

	if (status == MH_ERROR_NONE && progress != NULL)
		status = pack_directory(&tree, "", 0, error_out);

	tree.counting = 0;

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(context, MH_EXTRACT_WRITE_PACKAGE, NULL, 0,
			tree.total, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_zip_writer_open(stream, &tree.writer, error_out);

	if (status == MH_ERROR_NONE)
		status = pack_directory(&tree, "", 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_zip_writer_finish(tree.writer, error_out);

	mh_zip_writer_close(tree.writer);

	return status;
}

enum mh_error_code
mh_extract_pack(const char *source, const char *destination,
	const struct mh_extract_options *options, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	struct mh_data_stream *stream;
	struct mh_data_file *file;
	const char *name;
	enum mh_error_code status;

	directory = NULL;
	stream = NULL;
	file = NULL;
	status = mh_extract_package_parent(
		destination, source, &directory, &name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_open(directory, &stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_package_write(
			source, stream, options, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_seal(stream, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_POLL, NULL, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_create(
			stream, name, NULL, NULL, &file, error_out);

	mh_data_file_close(file);
	mh_data_stream_close(stream);
	mh_data_directory_close(directory);

	return status;
}

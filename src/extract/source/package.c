#include <stdlib.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/source/package.h"

struct package_copy {
	const struct mh_resource_source	*resources;
	const char			*source, *destination;

	mh_extract_progress		progress;
	void				*context;
	size_t				completed, total;
	int				counting;
};

struct copy_directory {
	struct package_copy	*copy;
	const char		*relative;
	unsigned int		depth;
	enum mh_error_code	status;
	struct mh_error		error;
};

static enum mh_error_code	copy_tree(struct package_copy *copy,
	const char *relative, unsigned int depth, struct mh_error *error_out);

static enum mh_error_code
copy_member(struct package_copy *copy, const char *relative, unsigned int depth,
	struct mh_error *error_out)
{
	char *path;
	struct mh_file bytes;
	unsigned long size;
	enum mh_error_code status;

	path = NULL;
	status = mh_extract_join(copy->source, relative, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Whole-resource reads verify the ZIP CRC before any patch can use it.
	 */
	if (copy->counting)
		status = mh_resource_source_size(
			copy->resources, path, &size, error_out);
	else
		status = mh_resource_source_read(
			copy->resources, path, &bytes, error_out);
	free(path);

	if (status == MH_ERROR_NOT_FOUND)
		return copy_tree(copy, relative, depth + 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (copy->counting) {
		if (copy->total == (size_t)-1)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many package entries");

		copy->total++;

		if (copy->progress != NULL)
			return copy->progress(copy->context, MH_EXTRACT_POLL,
				NULL, 0, 0, error_out);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_extract_write(
		copy->destination, relative, &bytes, error_out);
	free(bytes.data);

	if (status == MH_ERROR_NONE && copy->progress != NULL)
		status = copy->progress(copy->context, MH_EXTRACT_READ_PACKAGE,
			relative, ++copy->completed, copy->total, error_out);

	return status;
}

static enum mh_directory_action
copy_child(void *context, const char *name)
{
	struct copy_directory *directory;
	char *relative;

	directory = context;
	relative = NULL;

	/* The ZIP provider has already rejected unsafe or ambiguous paths. */
	if (directory->relative[0] == 0)
		directory->status = copy_member(directory->copy, name,
			directory->depth, &directory->error);
	else {
		directory->status = mh_extract_join(directory->relative, name,
			&relative, &directory->error);

		if (directory->status == MH_ERROR_NONE)
			directory->status = copy_member(directory->copy,
				relative, directory->depth, &directory->error);
	}

	free(relative);

	return directory->status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
						: MH_DIRECTORY_FAIL;
}

static enum mh_error_code
copy_tree(struct package_copy *copy, const char *relative, unsigned int depth,
	struct mh_error *error_out)
{
	struct copy_directory directory;
	char *path;
	enum mh_error_code status;

	if (depth > 64)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"package directory nesting exceeds limit");

	path = NULL;
	status = MH_ERROR_NONE;

	if (!copy->counting)
		status = mh_extract_join(
			copy->destination, relative, &path, error_out);

	if (status == MH_ERROR_NONE && !copy->counting)
		status = mh_user_data_create_directory(
			mh_filesystem_user_data(), path, error_out);

	free(path);

	if (status != MH_ERROR_NONE)
		return status;

	directory.copy = copy;
	directory.relative = relative;
	directory.depth = depth;
	directory.status = MH_ERROR_NONE;
	path = NULL;
	status = mh_extract_join(copy->source, relative, &path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_list(copy->resources, path,
			copy_child, &directory, error_out);

	free(path);

	if (directory.status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &directory.error);

	return status;
}

enum mh_error_code
mh_extract_package_unpack(const char *source, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct package_copy copy;
	enum mh_error_code status;

	copy.resources = NULL;
	copy.source = source;
	copy.destination = staging;
	copy.progress = progress;
	copy.context = context;
	copy.completed = 0;
	copy.total = 0;
	copy.counting = 1;
	status = mh_zip_resource_provider()->open(
		mh_filesystem_resources(), source, &copy.resources, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = copy_tree(&copy, "", 0, error_out);

	copy.counting = 0;

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(context, MH_EXTRACT_READ_PACKAGE, NULL, 0,
			copy.total, error_out);

	if (status == MH_ERROR_NONE)
		status = copy_tree(&copy, "", 0, error_out);

	mh_zip_resource_provider()->close(copy.resources);

	return status;
}

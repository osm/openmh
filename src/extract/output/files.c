#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_staging.h"
#include "backends/storage/filesystem_tree.h"
#include "engine/archive/path.h"
#include "engine/assets/path_installation.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "extract/output/files.h"

struct copy_progress {
	mh_extract_progress	progress;
	void			*context;
	size_t			completed;
};

struct publish_directory {
	struct mh_filesystem_staging	*staging;
	enum mh_error_code		status;
	struct mh_error			*error_out;
};

/*
 * Resolve each existing component before creating missing ones. Patch member
 * spelling must not create a second Cars/cars tree on case-sensitive hosts.
 */
static enum mh_error_code
output_path(const char *root, const char *relative, char **output,
	struct mh_error *error_out)
{
	char part[MH_ARCHIVE_PATH_SIZE], *path, *resolved;
	const char *scan;
	size_t capacity, length;
	int found;
	enum mh_error_code status;

	if (strlen(root) > (size_t)-1 - strlen(relative) - 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"extraction path is too long");

	capacity = strlen(root) + strlen(relative) + 2;
	path = malloc(capacity);
	resolved = malloc(capacity);

	if (path == NULL || resolved == NULL) {
		free(path);
		free(resolved);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resolving extraction path");
	}

	status = mh_text_copy(path, capacity, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(
			mh_filesystem_user_data(), root, error_out);

	scan = relative;

	while (*scan != 0 && status == MH_ERROR_NONE) {
		length = strcspn(scan, "/");

		if (length == 0 || length >= sizeof(part)) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid extraction component");
			break;
		}

		memcpy(part, scan, length);
		part[length] = 0;
		status = mh_asset_path_find_from(mh_filesystem_resources(),
			path, part, resolved, capacity, &found, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (found)
			status = mh_text_copy(
				path, capacity, resolved, error_out);
		else {
			status = mh_text_append(path, capacity, "/", error_out);

			if (status == MH_ERROR_NONE)
				status = mh_text_append(
					path, capacity, part, error_out);
		}

		if (status != MH_ERROR_NONE)
			break;

		scan += length;

		if (*scan == '/') {
			scan++;
			status = mh_user_data_create_directory(
				mh_filesystem_user_data(), path, error_out);
		}
	}

	free(resolved);

	if (status != MH_ERROR_NONE) {
		free(path);
		return status;
	}

	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_write(const char *root, const char *relative,
	const struct mh_file *file, struct mh_error *error_out)
{
	char *path;
	enum mh_error_code status;

	status = output_path(root, relative, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_replace(mh_filesystem_user_data(), path,
		file->data, file->size, error_out);
	free(path);

	return status;
}

static enum mh_error_code
copy_step(void *context, const char *name, struct mh_error *error_out)
{
	struct copy_progress *copy;

	copy = context;

	if (copy->progress != NULL)
		return copy->progress(copy->context, MH_EXTRACT_COPY, name,
			++copy->completed, 0, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_copy(const char *source, const char *destination,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct copy_progress copy;
	enum mh_error_code status;

	copy.progress = progress;
	copy.context = context;
	copy.completed = 0;
	status = mh_user_data_create_directory(
		mh_filesystem_user_data(), destination, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_filesystem_copy_tree(
			source, destination, copy_step, &copy, error_out);

	return status;
}

static enum mh_directory_action
remove_original(void *context, const char *name)
{
	struct publish_directory *publish;

	publish = context;

	if (!mh_ascii_equal(name, MH_ASSET_ORIGINAL_DIR))
		return MH_DIRECTORY_CONTINUE;

	publish->status = mh_filesystem_staging_remove(
		publish->staging, name, publish->error_out);

	return publish->status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
						: MH_DIRECTORY_STOP;
}

enum mh_error_code
mh_extract_publish_directory(struct mh_filesystem_staging *staging,
	int keep_original, struct mh_error *error_out)
{
	struct publish_directory publish;
	struct mh_error scan_error;
	enum mh_error_code status;

	if (!keep_original) {
		publish.staging = staging;
		publish.status = MH_ERROR_NONE;
		publish.error_out = error_out;
		status = mh_resource_source_list(mh_filesystem_resources(),
			mh_filesystem_staging_path(staging), remove_original,
			&publish, &scan_error);

		if (publish.status != MH_ERROR_NONE)
			return publish.status;

		if (status != MH_ERROR_NONE)
			return mh_error_forward(error_out, &scan_error);
	}

	return mh_filesystem_staging_publish(staging, error_out);
}

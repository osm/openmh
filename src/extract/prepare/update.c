#include "backends/storage/filesystem_staging.h"
#include "extract/output/files.h"
#include "extract/output/package.h"
#include "extract/prepare/patch.h"
#include "extract/prepare/update.h"
#include "extract/source/package.h"

static enum mh_error_code
patch_staged_game(const char *patch, const char *root,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_extract_patch_validate(root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_patch_apply(
			patch, root, 0, progress, context, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_POLL, NULL, 0, 0, error_out);

	return status;
}

enum mh_error_code
mh_extract_update_directory(const char *patch, const char *destination,
	int keep_original, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct mh_filesystem_staging *staging;
	const char *root;
	enum mh_error_code status;

	staging = NULL;
	status = mh_extract_patch_validate(destination, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_filesystem_staging_update(
			destination, &staging, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	root = mh_filesystem_staging_path(staging);
	status = mh_extract_copy(
		destination, root, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = patch_staged_game(
			patch, root, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_publish_directory(
			staging, keep_original, error_out);

	mh_filesystem_staging_close(staging);

	return status;
}

static enum mh_error_code
replace_package(const char *root, struct mh_data_directory *directory,
	struct mh_data_file *original, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct mh_data_stream *stream;
	enum mh_error_code status;

	stream = NULL;
	status = mh_data_stream_open(directory, &stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_package_write(
			root, stream, options, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_seal(stream, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_POLL, NULL, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_replace(
			stream, original, NULL, NULL, error_out);

	mh_data_stream_close(stream);

	return status;
}

static enum mh_error_code
prepare_package_patch(const char *patch, const char *destination,
	const char *root, struct mh_data_file *original,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_extract_package_unpack(
		destination, root, progress, context, error_out);

	/* The copy must come from the same revision we will later replace. */
	if (status == MH_ERROR_NONE)
		status = mh_data_file_validate(original, error_out);

	if (status == MH_ERROR_NONE)
		status = patch_staged_game(
			patch, root, progress, context, error_out);

	return status;
}

enum mh_error_code
mh_extract_update_package(const char *patch, const char *destination,
	const struct mh_extract_options *options, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	struct mh_filesystem_staging *staging;
	struct mh_data_directory *directory;
	struct mh_data_file *original;
	const char *name, *root;
	enum mh_error_code status;

	staging = NULL;
	directory = NULL;
	original = NULL;
	status = mh_extract_package_parent(
		destination, patch, &directory, &name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_open_file(
			directory, name, &original, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_filesystem_staging_scratch(
			destination, &staging, error_out);

	if (status == MH_ERROR_NONE) {
		root = mh_filesystem_staging_path(staging);
		status = prepare_package_patch(patch, destination, root,
			original, progress, context, error_out);

		if (status == MH_ERROR_NONE)
			status = replace_package(root, directory, original,
				options, progress, context, error_out);
	}

	mh_data_file_close(original);
	mh_data_directory_close(directory);
	mh_filesystem_staging_close(staging);

	return status;
}

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_staging.h"
#include "extract/output/files.h"
#include "extract/output/package.h"
#include "extract/prepare/demo.h"
#include "extract/prepare/install.h"
#include "extract/prepare/patch.h"
#include "extract/prepare/retail.h"
#include "extract/prepare/update.h"
#include "extract/source/demo_unpack.h"
#include "extract/source/disc.h"
#include "extract/source/drive.h"
#include "extract/source/input.h"
#include "extract/source/patch.h"

static enum mh_error_code
extract_demo(const char *source, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	const struct mh_demo_package *package;
	enum mh_error_code status;

	status = mh_extract_demo_unpack(
		source, staging, &package, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_demo_prepare(package, staging, error_out);

	return status;
}

static enum mh_error_code
extract_retail(const struct mh_extract_input *input, const char *staging,
	int include_music, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	int has_audio;
	enum mh_error_code status;

	has_audio = 0;

	if (input->kind == MH_EXTRACT_INPUT_DISC)
		status = mh_extract_disc_files(
			input->path, staging, progress, context, error_out);
	else if (input->kind == MH_EXTRACT_INPUT_DEVICE) {
		has_audio = 1;
		status = mh_extract_drive_unpack(input->path, staging,
			include_music, progress, context, error_out);
	} else
		status = mh_extract_disc_unpack(input->path, staging,
			&has_audio, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_retail_prepare(staging, error_out);

	if (status == MH_ERROR_NONE && !has_audio && include_music &&
		progress != NULL)
		status = progress(context, MH_EXTRACT_NOTICE,
			"This source has no CD audio; music will not be included.",
			0, 0, error_out);

	return status;
}

static enum mh_error_code
extract_base(const struct mh_extract_input *input, const char *staging,
	const struct mh_extract_options *options, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	unsigned char signature[2];
	enum mh_error_code status;

	if (input->kind == MH_EXTRACT_INPUT_GAME)
		return mh_extract_copy(
			input->path, staging, progress, context, error_out);

	if (input->kind == MH_EXTRACT_INPUT_DISC ||
		input->kind == MH_EXTRACT_INPUT_DEVICE)
		return extract_retail(input, staging, !options->exclude_music,
			progress, context, error_out);

	status = mh_resource_source_read_at(mh_filesystem_resources(),
		input->path, 0, signature, sizeof(signature), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (signature[0] == 'M' && signature[1] == 'Z')
		return extract_demo(
			input->path, staging, progress, context, error_out);

	return extract_retail(input, staging, !options->exclude_music, progress,
		context, error_out);
}

static enum mh_error_code
create_installation(const struct mh_extract_input *input,
	const char *const *patches, size_t patch_count, const char *destination,
	int package, int patch, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct mh_filesystem_staging *staging;
	const char *root;
	size_t i;
	enum mh_error_code status;

	staging = NULL;
	status = mh_filesystem_staging_open(destination, &staging, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	root = mh_filesystem_staging_path(staging);

	if (patch)
		status = mh_extract_patch_apply(
			input->path, root, 1, progress, context, error_out);
	else
		status = extract_base(
			input, root, options, progress, context, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < patch_count; i++) {
		if (progress != NULL)
			status = progress(context, MH_EXTRACT_POLL, NULL, 0, 0,
				error_out);

		if (status == MH_ERROR_NONE)
			status = mh_extract_patch_validate(root, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_extract_patch_apply(patches[i], root, 0,
				progress, context, error_out);
	}

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_POLL, NULL, 0, 0, error_out);

	if (status == MH_ERROR_NONE) {
		if (package)
			status = mh_extract_pack(root, destination, options,
				progress, context, error_out);
		else
			status = mh_extract_publish_directory(
				staging, options->keep_original, error_out);
	}

	mh_filesystem_staging_close(staging);

	return status;
}

enum mh_error_code
mh_extract_install(const struct mh_extract_input *input,
	const char *const *patches, size_t patch_count, const char *destination,
	int package, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context, int *patched,
	struct mh_error *error_out)
{
	int patch, recognized;
	unsigned long size;
	size_t i;
	enum mh_error_code status;

	patch = 0;
	status = MH_ERROR_NONE;

	if (input->kind == MH_EXTRACT_INPUT_FILE)
		status = mh_extract_patch_identify(
			input->path, &patch, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (patch && patch_count != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"the first source must be the base game, followed by patch installers");

	/*
	 * Reject unusable extra inputs before reading a disc or creating
	 * output.
	 */
	for (i = 0; i < patch_count; i++) {
		status = mh_extract_patch_identify(
			patches[i], &recognized, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!recognized)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"additional sources must be supported patch installers; use cumulative motorp30.exe");
	}

	if (patch && package &&
		mh_resource_source_size(mh_filesystem_resources(), destination,
			&size, NULL) == MH_ERROR_NOT_FOUND)
		status = create_installation(input, patches, patch_count,
			destination, package, 1, options, progress, context,
			error_out);
	else if (patch && package)
		status = mh_extract_update_package(input->path, destination,
			options, progress, context, error_out);
	else if (patch)
		status = mh_extract_update_directory(input->path, destination,
			options->keep_original, progress, context, error_out);
	else
		status = create_installation(input, patches, patch_count,
			destination, package, 0, options, progress, context,
			error_out);

	if (status == MH_ERROR_NONE)
		*patched = patch || patch_count != 0;

	return status;
}

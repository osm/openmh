#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/prepare/patch.h"
#include "extract/source/patch.h"

/*
 * Test-only patch decoder. The CLI, staging, copying and package writer are
 * production code; no original executables or installer archives are needed.
 * Records contain a header followed by the required and resulting revisions.
 */
static const char header[] = "MH TEST PATCH 1\n";

static enum mh_error_code
read_patch(const char *path, struct mh_file *file, struct mh_error *error_out)
{
	return mh_resource_source_read_bounded(
		mh_filesystem_resources(), path, 512, file, error_out);
}

enum mh_error_code
__wrap_mh_extract_patch_identify(
	const char *path, int *found, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = read_patch(path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*found = file.size == sizeof(header) + 1 &&
		memcmp(file.data, header, sizeof(header) - 1) == 0;
	free(file.data);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
revision(const char *root, unsigned char *value, struct mh_error *error_out)
{
	char *path;
	enum mh_error_code status;

	path = NULL;
	status = mh_extract_join(root, "Game/revision", &path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_at(mh_filesystem_resources(),
			path, 0, value, 1, error_out);

	free(path);

	return status;
}

enum mh_error_code
__wrap_mh_extract_patch_validate(const char *root, struct mh_error *error_out)
{
	unsigned char value;
	enum mh_error_code status;

	status = revision(root, &value, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (value == '2')
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"fixture is already patched");

	if (value != '0' && value != '1')
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"fixture edition is unsupported");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
__wrap_mh_extract_patch_apply(const char *source, const char *root, int overlay,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct mh_file patch, file;
	unsigned char before, after;
	enum mh_error_code status;

	status = read_patch(source, &patch, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (patch.size != sizeof(header) + 1) {
		free(patch.data);
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid fixture patch");
	}

	before = patch.data[sizeof(header) - 1];
	after = patch.data[sizeof(header)];
	free(patch.data);

	if (!overlay) {
		unsigned char actual;

		status = revision(root, &actual, error_out);

		if (status == MH_ERROR_NONE && before != actual)
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"fixture patches are out of order");
	}

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_PATCH, source, 0, 1, error_out);

	file.data = &after;
	file.size = 1;

	if (status == MH_ERROR_NONE)
		status = mh_extract_write(
			root, "Game/revision", &file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_write(
			root, "Original/patch.bin", &file, error_out);

	/*
	 * The track definition allows the real package probe to recognize
	 * fixtures.
	 */
	if (status == MH_ERROR_NONE && overlay)
		status = mh_extract_write(
			root, "Game/fixture.trk", &file, error_out);

	if (status == MH_ERROR_NONE && after == 'x')
		status = mh_error_set(error_out, MH_ERROR_IO,
			"fixture patch failed after writing");

	if (status == MH_ERROR_NONE && after == 'c') {
		raise(SIGINT);

		if (progress != NULL)
			status = progress(context, MH_EXTRACT_POLL, NULL, 0, 0,
				error_out);
	}

	return status;
}

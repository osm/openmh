#include <stdlib.h>
#include <string.h>

#include "backends/archive/mscab_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/assets/path_installation.h"
#include "engine/core/text.h"
#include "engine/storage/digest.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/source/packages.h"
#include "extract/source/patch.h"

static enum mh_error_code
identify_digest(unsigned long size, const char *digest, int *found,
	struct mh_error *error_out)
{
	const struct mh_patch_package *package;

	package = mh_patch_package_find(size, digest);
	*found = package != NULL && package->kind == MH_PATCH_CUMULATIVE;

	if (package == NULL || *found)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (package->kind == MH_PATCH_EXECUTABLE_ONLY)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"patch 3.1 only replaces the Windows executable; use cumulative motorp30.exe for this port's assets");

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"earlier patch package: use cumulative motorp30.exe, which includes its asset updates");
}

enum mh_error_code
mh_extract_patch_identify(
	const char *path, int *found, struct mh_error *error_out)
{
	char digest[MH_SHA256_TEXT_SIZE];
	unsigned long size;
	enum mh_error_code status;

	*found = 0;
	status = mh_resource_source_size(
		mh_filesystem_resources(), path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Disc images cannot be one of these small installer packages. */
	if (size > 64UL * 1024UL * 1024UL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_resource_sha256(
		mh_filesystem_resources(), path, digest, &size, error_out);

	if (status == MH_ERROR_NONE)
		status = identify_digest(size, digest, found, error_out);

	return status;
}

static enum mh_error_code
read_inner(struct mh_archive *archive, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct mh_archive_entry *entry;
	size_t i;

	for (i = 0; i < mh_archive_count(archive); i++) {
		entry = mh_archive_entry(archive, i);

		if (mh_ascii_equal(entry->path, "disk1/data1.cab"))
			return mh_archive_read(archive, i,
				MH_RESOURCE_READ_LIMIT, output, error_out);
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "patch is missing data1.cab");
}

enum mh_error_code
mh_extract_patch_read(const char *path, const char *staging,
	struct mh_file *cabinet, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct mh_file package;
	const struct mh_patch_package *supported;
	struct mh_archive *archive;
	struct mh_sha256 hash;
	unsigned char digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE], *root;
	int found;
	enum mh_error_code status;

	supported = mh_patch_package_supported();
	status = mh_resource_source_read_bounded(mh_filesystem_resources(),
		path, supported->size, &package, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, package.data, package.size);
	mh_sha256_finish(&hash, digest);
	mh_sha256_text(digest, text);
	archive = NULL;
	root = NULL;
	status = identify_digest(
		(unsigned long)package.size, text, &found, error_out);

	if (status == MH_ERROR_NONE && !found)
		status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unrecognized or changed patch 3.0 installer");

	if (status == MH_ERROR_NONE)
		status = mh_mscab_open(package.data + supported->cabinet_offset,
			supported->cabinet_size, &archive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_join(
			staging, MH_ASSET_PATCH_DIR, &root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_write(
			root, MH_ASSET_PATCH_INSTALLER, &package, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_unpack(archive, root, MH_EXTRACT_INSTALLER,
			progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = read_inner(archive, cabinet, error_out);

	free(root);
	mh_archive_close(archive);
	free(package.data);

	return status;
}

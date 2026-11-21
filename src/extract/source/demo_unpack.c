#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/assets/path_installation.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/source/demo.h"
#include "extract/source/demo_unpack.h"
#include "extract/source/installer.h"

static enum mh_error_code
extract_support(const struct mh_extract_demo *demo, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct mh_archive *archive;
	char *root;
	size_t offset;
	int compressed;
	enum mh_error_code status;

	compressed = demo->package->container == MH_PACKAGE_ZIP_CABINET;
	offset = demo->package->wrapper_offset;

	if (offset >= demo->file.size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo installer records");

	archive = NULL;
	root = NULL;
	status = mh_unshield_sfx_open(demo->file.data + offset,
		demo->file.size - offset, compressed, &archive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_join(
			staging, MH_ASSET_DEMO_DIR, &root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_unpack(archive, root, MH_EXTRACT_UNPACK,
			progress, context, error_out);

	free(root);
	mh_archive_close(archive);

	return status;
}

static enum mh_error_code
open_demo_archive(const struct mh_extract_demo *demo, struct mh_file *expanded,
	struct mh_archive **archive, struct mh_error *error_out)
{
	const unsigned char *bytes;
	size_t size;
	enum mh_error_code status;

	bytes = demo->file.data + demo->package->cabinet_offset;
	size = demo->file.size - demo->package->cabinet_offset;

	if (demo->package->container == MH_PACKAGE_ZIP_CABINET) {
		status = mh_unshield_zip_cabinet(
			bytes, size, expanded, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		bytes = expanded->data;
		size = expanded->size;
	}

	return mh_unshield_open(bytes, size, archive, error_out);
}

static enum mh_error_code
retain_demo(const struct mh_extract_demo *demo, const struct mh_file *expanded,
	const char *staging, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	const unsigned char *bytes;
	size_t size;
	enum mh_error_code status;

	bytes = demo->file.data + demo->package->cabinet_offset;
	size = demo->file.size - demo->package->cabinet_offset;

	if (expanded->data != NULL) {
		bytes = expanded->data;
		size = expanded->size;
	}

	status = mh_extract_installer_components(bytes, size, staging,
		MH_ASSET_DEMO_COMPONENTS_DIR, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = extract_support(
			demo, staging, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_write(staging, MH_ASSET_DEMO_INSTALLER,
			&demo->file, error_out);

	return status;
}

enum mh_error_code
mh_extract_demo_unpack(const char *source, const char *staging,
	const struct mh_demo_package **package, mh_extract_progress progress,
	void *context, struct mh_error *error_out)
{
	struct mh_extract_demo demo;
	struct mh_archive *archive;
	struct mh_file expanded;

	enum mh_error_code status;

	memset(&demo, 0, sizeof(demo));
	memset(&expanded, 0, sizeof(expanded));
	archive = NULL;
	status = mh_extract_demo_read(
		mh_filesystem_resources(), source, &demo, error_out);

	if (status == MH_ERROR_NONE)
		status = open_demo_archive(
			&demo, &expanded, &archive, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_extract_unpack(archive, staging, MH_EXTRACT_UNPACK,
			progress, context, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = retain_demo(&demo, &expanded, staging, progress,
			context, error_out);

	if (status == MH_ERROR_NONE)
		*package = demo.package;

	mh_archive_close(archive);
	free(expanded.data);
	mh_extract_demo_free(&demo);

	return status;
}

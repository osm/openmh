#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/catalog/adp_internal.h"
#include "engine/assets/path_catalog.h"
#include "engine/assets/profile.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

static int
compare_files(const void *left, const void *right)
{
	return mh_ascii_compare((const char *)left, (const char *)right);
}

static enum mh_directory_action
collect_file(void *context, const char *name)
{
	struct mh_profile_catalog *catalog;
	size_t length;

	catalog = (struct mh_profile_catalog *)context;
	length = strlen(name);

	if (length < (sizeof(MH_ASSET_PROFILE_EXTENSION) - 1) ||
		mh_ascii_compare(name + length -
				(sizeof(MH_ASSET_PROFILE_EXTENSION) - 1),
			MH_ASSET_PROFILE_EXTENSION) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (catalog->count == MH_PROFILE_CATALOG_LIMIT ||
		length >= sizeof(catalog->files[0]))
		return MH_DIRECTORY_FAIL;

	strcpy(catalog->files[catalog->count++], name);

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
load_profile(const struct mh_resource_source *source, const char *root,
	const char *name, struct mh_profile *profile,
	struct mh_error *error_out)
{
	struct mh_file file;
	char relative[288], path[1024];
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	if (strlen(name) >= MH_PROFILE_REFERENCE_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"profile reference too long");

	sprintf(relative, MH_ASSET_PROFILE_FORMAT, name);
	status = mh_asset_path_from(
		source, root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(source, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_tbl_decode(file.data, file.size, &text, &size, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_profile_adp_decode(text, size, profile, error_out);
	free(text);

	return status;
}

enum mh_error_code
mh_profile_adp_load(const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	char name[MH_PROFILE_REFERENCE_BYTES];
	size_t length;

	length = strlen(reference);

	if (length >= (sizeof(MH_ASSET_PROFILE_EXTENSION) - 1) &&
		mh_ascii_equal(reference + length -
				(sizeof(MH_ASSET_PROFILE_EXTENSION) - 1),
			MH_ASSET_PROFILE_EXTENSION))
		return load_profile(
			resources, root, reference, output, error_out);

	if (length > sizeof(name) - sizeof(MH_ASSET_PROFILE_EXTENSION))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"profile reference too long");

	memcpy(name, reference, length);
	memcpy(name + length, MH_ASSET_PROFILE_EXTENSION,
		sizeof(MH_ASSET_PROFILE_EXTENSION));

	return load_profile(resources, root, name, output, error_out);
}

enum mh_error_code
mh_profile_adp_catalog_load(const struct mh_resource_source *source,
	const char *root, struct mh_profile_catalog *output,
	struct mh_error *error_out)
{
	struct mh_profile_catalog *catalog;
	char directory[1024];
	unsigned int i;
	enum mh_error_code status;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile catalog input");

	catalog = (struct mh_profile_catalog *)calloc(1, sizeof(*catalog));

	if (catalog == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading profile catalog");

	status = mh_asset_path_from(source, root, MH_ASSET_PROFILES_DIR,
		directory, sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	if (mh_resource_source_list(source, directory, collect_file, catalog,
		NULL) != MH_ERROR_NONE) {
		status = mh_error_set(error_out, MH_ERROR_IO,
			"cannot enumerate bounded ADP catalog");
		goto done;
	}

	if (catalog->count == 0) {
		status = mh_error_set(
			error_out, MH_ERROR_INVALID, "empty profile catalog");
		goto done;
	}

	qsort(catalog->files, catalog->count, sizeof(catalog->files[0]),
		compare_files);

	for (i = 0; i < catalog->count; i++) {
		if (i != 0 &&
			mh_ascii_compare(catalog->files[i - 1],
				catalog->files[i]) == 0) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous ADP filename");
			goto done;
		}

		status = load_profile(source, root, catalog->files[i],
			&catalog->profiles[i], error_out);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	*output = *catalog;

done:
	free(catalog);

	return status;
}

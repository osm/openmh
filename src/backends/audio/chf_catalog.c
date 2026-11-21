#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/chf_internal.h"
#include "engine/assets/horn.h"
#include "engine/assets/path_audio.h"
#include "engine/audio/horn.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

struct horn_files {
	unsigned int	count;
	char		names[MH_HORNS][256];
};

static int
compare_files(const void *left, const void *right)
{
	return mh_ascii_compare((const char *)left, (const char *)right);
}

static enum mh_directory_action
collect_file(void *context, const char *name)
{
	struct horn_files *files;
	size_t length;

	files = (struct horn_files *)context;
	length = strlen(name);

	if (length < 4 || mh_ascii_compare(name + length - 4, ".chf") != 0)
		return MH_DIRECTORY_CONTINUE;

	if (files->count == MH_HORNS || length >= sizeof(files->names[0]))
		return MH_DIRECTORY_FAIL;

	strcpy(files->names[files->count++], name);

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
load_horn(const struct mh_resource_source *source, const char *root,
	const char *name, unsigned int index, struct mh_horn *horn,
	struct mh_error *error_out)
{
	struct mh_file file;
	char relative[288], path[1024];
	enum mh_error_code status;

	sprintf(relative, MH_ASSET_HORN_FORMAT, name);
	memset(&file, 0, sizeof(file));
	status = mh_asset_path_from(
		source, root, relative, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_resource_source_read(source, path, &file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_horn_chf_decode(
			file.data, file.size, index, horn, error_out);

	free(file.data);

	return status;
}

enum mh_error_code
mh_horn_chf_catalog_load(const struct mh_resource_source *source,
	const char *root, struct mh_horn_catalog *output,
	struct mh_error *error_out)
{
	struct horn_files files;
	struct mh_horn_catalog catalog;
	char directory[1024];
	unsigned int i, found;
	enum mh_error_code status;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing horn catalog output");

	memset(&files, 0, sizeof(files));
	memset(&catalog, 0, sizeof(catalog));
	status = mh_asset_path_from(source, root, MH_ASSET_HORNS_DIR, directory,
		sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_resource_source_list(source, directory, collect_file, &files,
		NULL) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot enumerate bounded CHF catalog");

	if (files.count == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty horn catalog");

	qsort(files.names, files.count, sizeof(files.names[0]), compare_files);

	for (i = 0; i < files.count; i++) {
		if (i != 0 &&
			mh_ascii_compare(files.names[i - 1], files.names[i]) ==
				0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous CHF filename");

		status = load_horn(source, root, files.names[i], i,
			&catalog.horns[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	catalog.count = files.count;

	for (i = 0; i < catalog.count; i++) {
		status = mh_horn_find(
			&catalog, catalog.horns[i].name, &found, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*output = catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <stdlib.h>
#include <string.h>

#include "engine/storage/asset_path.h"
#include "engine/storage/overlay.h"
#include "engine/storage/vfs.h"
#include "interfaces/storage/data_directory.h"

struct mh_vfs {
	const struct mh_resource_source			*resources;
	const struct mh_resource_archive_provider	*archives;
	char						*root, *installation;
	int						mounted;
	struct mh_resource_overlay			*overlay;
	struct mh_vfs					**children;
	size_t						count;
};

void
mh_vfs_close(struct mh_vfs *vfs)
{
	size_t i;

	if (vfs == NULL)
		return;

	mh_resource_overlay_close(vfs->overlay);

	for (i = 0; i < vfs->count; i++)
		mh_vfs_close(vfs->children[i]);

	free(vfs->children);

	if (vfs->mounted)
		vfs->archives->close(vfs->resources);

	free(vfs->root);
	free(vfs->installation);
	free(vfs);
}

static enum mh_error_code
select_root(
	struct mh_vfs *vfs, const char *location, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	unsigned long size;
	size_t length;
	enum mh_error_code status;

	length = strlen(location);

	if (length > (size_t)-1 - 2)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "asset location too long");

	vfs->root = malloc(length + 1);
	vfs->installation = malloc(length + 2);

	if (vfs->root == NULL || vfs->installation == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory selecting assets");

	strcpy(vfs->root, location);
	strcpy(vfs->installation, location);

	if (vfs->archives == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	directory = NULL;
	status = mh_resource_source_open_directory(
		vfs->resources, location, &directory, error_out);

	if (status == MH_ERROR_NONE) {
		mh_data_directory_close(directory);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/*
	 * A package is itself a regular resource. Propagate its read failure.
	 */
	status = mh_resource_source_size(
		vfs->resources, location, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = mh_path_directory_length(location);

	if (length == 0)
		strcpy(vfs->installation, ".");
	else
		vfs->installation[length] = 0;

	status = vfs->archives->open(
		vfs->resources, vfs->root, &vfs->resources, error_out);

	if (status == MH_ERROR_NONE)
		vfs->mounted = 1;

	return status;
}

enum mh_error_code
mh_vfs_open(const struct mh_resource_source *source,
	const struct mh_resource_archive_provider *archives,
	const char *location, struct mh_vfs **output,
	struct mh_error *error_out)
{
	struct mh_vfs *vfs;
	enum mh_error_code status;

	if (source == NULL || location == NULL || location[0] == 0 ||
		output == NULL ||
		(archives != NULL &&
			(archives->open == NULL || archives->close == NULL)))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid asset mount");

	vfs = calloc(1, sizeof(*vfs));

	if (vfs == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory mounting assets");

	vfs->resources = source;
	vfs->archives = archives;
	status = select_root(vfs, location, error_out);

	if (status != MH_ERROR_NONE) {
		mh_vfs_close(vfs);
		return status;
	}

	*output = vfs;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_resource_source *
mh_vfs_resources(const struct mh_vfs *vfs)
{
	return vfs->resources;
}

const char *
mh_vfs_root(const struct mh_vfs *vfs)
{
	return vfs->root;
}

const char *
mh_vfs_installation(const struct mh_vfs *vfs)
{
	return vfs->installation;
}

/* Transfer child mounts only after the entire combined namespace validates. */
enum mh_error_code
mh_vfs_combine(struct mh_vfs **mounts, const int *replace, size_t count,
	struct mh_vfs **output, struct mh_error *error_out)
{
	struct mh_vfs *vfs;
	struct mh_resource_layer *layers;
	size_t i, j;
	enum mh_error_code status;

	if (mounts == NULL || count == 0 || count > 128 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid package collection");

	for (i = 0; i < count; i++) {
		if (mounts[i] == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing child mount");

		for (j = 0; j < i; j++)
			if (mounts[i] == mounts[j])
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate child mount");
	}

	vfs = calloc(1, sizeof(*vfs));
	layers = calloc(count, sizeof(*layers));

	if (vfs == NULL || layers == NULL) {
		free(vfs);
		free(layers);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory combining packages");
	}

	vfs->children = malloc(count * sizeof(*vfs->children));
	vfs->root = malloc(strlen(mounts[0]->root) + 1);
	vfs->installation = malloc(strlen(mounts[0]->installation) + 1);

	if (vfs->children == NULL || vfs->root == NULL ||
		vfs->installation == NULL) {
		free(layers);
		mh_vfs_close(vfs);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory combining packages");
	}

	strcpy(vfs->root, mounts[0]->root);
	strcpy(vfs->installation, mounts[0]->installation);

	for (i = 0; i < count; i++) {
		layers[i].source = mounts[i]->resources;
		layers[i].root = mounts[i]->root;
		layers[i].replace = replace != NULL && replace[i];
	}

	status = mh_resource_overlay_open(
		layers, count, vfs->root, &vfs->overlay, error_out);
	free(layers);

	if (status != MH_ERROR_NONE) {
		mh_vfs_close(vfs);
		return status;
	}

	memcpy(vfs->children, mounts, count * sizeof(*mounts));
	vfs->count = count;
	vfs->resources = mh_resource_overlay_source(vfs->overlay);
	*output = vfs;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

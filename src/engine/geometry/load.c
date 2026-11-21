#include <stdlib.h>
#include <string.h>

#include "engine/geometry/load.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

#define GEOMETRY_PATH_BYTES 1024

static enum mh_error_code
resolve_reference(const char *extension,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out)
{
	char relative[GEOMETRY_PATH_BYTES];
	enum mh_error_code status;

	if (extension == NULL)
		return mh_asset_path_find_from(resources, root, reference, path,
			capacity, found, error_out);

	status = mh_asset_reference_extension(
		reference, extension, relative, sizeof(relative), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_asset_path_find_from(
		resources, root, relative, path, capacity, found, error_out);
}

enum mh_error_code
mh_model_path_find_from(const struct mh_model_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out)
{
	const char *extension;
	enum mh_error_code status;

	status = mh_model_decoder_extension(decoder, &extension, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return resolve_reference(extension, resources, root, reference, path,
		capacity, found, error_out);
}

enum mh_error_code
mh_model_load_from(const struct mh_model_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_model *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[GEOMETRY_PATH_BYTES];
	int found;
	enum mh_error_code status;

	status = mh_model_path_find_from(decoder, resources, root, reference,
		path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"asset path component not found");

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_model_decode(
		decoder, file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
world_path_find(const struct mh_world_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out)
{
	const char *extension;
	enum mh_error_code status;

	status = mh_world_decoder_extension(decoder, &extension, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return resolve_reference(extension, resources, root, reference, path,
		capacity, found, error_out);
}

enum mh_error_code
mh_world_load_from(const struct mh_world_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_world *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[GEOMETRY_PATH_BYTES];
	int found;
	enum mh_error_code status;

	status = world_path_find(decoder, resources, root, reference, path,
		sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"asset path component not found");

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_world_decode(
		decoder, file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

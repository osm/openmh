#include <stdlib.h>
#include <string.h>

#include "engine/collision/load.h"
#include "engine/storage/asset_path.h"
#include "interfaces/collision/decoder_backend.h"
#include "interfaces/collision/source_backend.h"
#include "interfaces/storage/resource.h"

#define COLLISION_PATH_BYTES 1024

static enum mh_error_code
resolve_reference(const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	const char *extension;
	struct mh_collision_decoder decoding;
	char relative[COLLISION_PATH_BYTES];
	enum mh_error_code status;

	if (source == NULL || source->decode == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"collision source is unavailable");

	decoding.context = source->context;
	decoding.extension = source->extension;
	decoding.decode = source->decode;
	status = mh_collision_decoder_extension(
		&decoding, &extension, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (extension == NULL)
		return mh_asset_path_from(
			resources, root, reference, path, capacity, error_out);

	status = mh_asset_reference_extension(
		reference, extension, relative, sizeof(relative), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_asset_path_from(
		resources, root, relative, path, capacity, error_out);
}

enum mh_error_code
mh_collision_file_read(const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	char path[COLLISION_PATH_BYTES];
	enum mh_error_code status;

	status = resolve_reference(source, resources, root, reference, path,
		sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read(resources, path, output, error_out);
}

enum mh_error_code
mh_collision_read_from(const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->decode == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"collision source is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid collision source request");

	if (source->read != NULL)
		return source->read(source->context, resources, root, reference,
			output, error_out);

	return mh_collision_file_read(
		source, resources, root, reference, output, error_out);
}

enum mh_error_code
mh_collision_source_decode(const struct mh_collision_source *source,
	const unsigned char *data, size_t size, struct mh_collision *output,
	struct mh_error *error_out)
{
	struct mh_collision_decoder decoder;

	if (source == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"collision source is unavailable");

	decoder.context = source->context;
	decoder.extension = source->extension;
	decoder.decode = source->decode;

	return mh_collision_decode(&decoder, data, size, output, error_out);
}

enum mh_error_code
mh_collision_load_from(const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_collision *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_collision_read_from(
		source, resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_collision_source_decode(
		source, file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

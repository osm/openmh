#include <stdlib.h>
#include <string.h>

#include "engine/image/load.h"
#include "engine/storage/asset_path.h"
#include "interfaces/image/decoder_backend.h"
#include "interfaces/storage/resource.h"

#define IMAGE_PATH_BYTES 1024

enum mh_error_code
mh_image_path_find_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out)
{
	const char *extension, *scan, *suffix, *basename;
	char relative[IMAGE_PATH_BYTES];
	size_t stem, length;
	enum mh_error_code status;

	status = mh_image_decoder_extension(decoder, &extension, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (extension == NULL)
		return mh_asset_path_find_from(resources, root, reference, path,
			capacity, found, error_out);

	if (reference == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing image reference");

	basename = reference;
	suffix = NULL;

	for (scan = reference; *scan != 0; scan++) {
		if (*scan == '/' || *scan == '\\') {
			basename = scan + 1;
			suffix = NULL;
		} else if (*scan == '.')
			suffix = scan;
	}

	if (*basename == 0 || suffix == basename ||
		(suffix != NULL && suffix[1] == 0))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid image reference");

	stem = (size_t)((suffix == NULL ? scan : suffix) - reference);
	length = strlen(extension);

	if (stem >= sizeof(relative) - length - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"image reference is too long");

	memcpy(relative, reference, stem);
	relative[stem] = '.';
	memcpy(relative + stem + 1, extension, length + 1);

	return mh_asset_path_find_from(
		resources, root, relative, path, capacity, found, error_out);
}

enum mh_error_code
mh_image_path_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int found;

	status = mh_image_path_find_from(decoder, resources, root, reference,
		path, capacity, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"asset path component not found");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_file_read(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *file, struct mh_error *error_out)
{
	char path[IMAGE_PATH_BYTES];
	enum mh_error_code status;

	status = mh_image_path_from(decoder, resources, root, reference, path,
		sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read(resources, path, file, error_out);
}

enum mh_error_code
mh_image_read_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *file, struct mh_error *error_out)
{
	if (decoder != NULL && decoder->read != NULL)
		return decoder->read(decoder->context, resources, root,
			reference, file, error_out);

	return mh_image_file_read(
		decoder, resources, root, reference, file, error_out);
}

enum mh_error_code
mh_image_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, const struct mh_image_decode_options *options,
	struct mh_image *output, struct mh_image_palette *palette_output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_image_read_from(
		decoder, resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode(decoder, file.data, file.size, options, output,
		palette_output, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_image_load_indexed_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_indexed_image *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_image_read_from(
		decoder, resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode_indexed(
		decoder, file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

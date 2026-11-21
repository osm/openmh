#include <stdlib.h>

#include "engine/image/load.h"
#include "engine/image/portrait.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_portrait_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;
	int found;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing results portrait output");

	status = mh_image_path_find_from(decoder, resources, root, reference,
		path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_portrait_blank(output, error_out);

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return mh_portrait_blank(output, error_out);

	status = mh_image_decode(
		decoder, file.data, file.size, NULL, &image, NULL, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_portrait_prepare(&image, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

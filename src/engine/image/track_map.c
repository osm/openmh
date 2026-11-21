#include <string.h>

#include "engine/image/track_map.h"

enum mh_error_code
mh_track_map_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_image_decode_options options;
	struct mh_image image;
	char relative[sizeof(track->map_name) + 4];
	size_t length, i;
	enum mh_error_code status;

	if (track == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track map request");

	memset(&image, 0, sizeof(image));

	if (track->map_name[0] == 0 && !track->has_map_info) {
		*output = image;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (track->map_name[0] == 0 || !track->has_map_info)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete track map configuration");

	for (length = 0; length < sizeof(track->map_name); length++)
		if (track->map_name[length] == 0)
			break;

	if (length == sizeof(track->map_name))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"track map name is too long");

	memcpy(relative, track->map_name, length);
	memcpy(relative + length, ".iff", 5);
	options.alpha = MH_IMAGE_ALPHA_INDEX_ZERO;
	options.palette = NULL;
	status = mh_image_load_from(decoder, resources, root, relative,
		&options, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < (size_t)image.width * image.height; i++)
		if (image.rgba[i * 4 + 3] == 0)
			memset(image.rgba + i * 4, 0, 3);

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

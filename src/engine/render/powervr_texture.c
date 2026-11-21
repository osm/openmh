#include <string.h>

#include "engine/render/mesh.h"
#include "engine/render/powervr_texture.h"

#define TEXTURE_MIN 32
#define TEXTURE_MAX 256

enum mh_error_code
mh_powervr_texture_size(unsigned int width, unsigned int height,
	unsigned int *side, struct mh_error *error_out)
{
	unsigned int size;

	if (side == NULL || width == 0 || height == 0 || width > TEXTURE_MAX ||
		height > TEXTURE_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR texture dimensions");

	/*
	 * Unatlased software-demo images may be smaller than PowerSGL accepts.
	 */
	size = TEXTURE_MIN;

	while (size < width || size < height)
		size *= 2;

	*side = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_powervr_texture_prepare(const struct mh_image *source,
	enum mh_texture_format format, struct mh_image *output,
	struct mh_error *error_out)
{
	unsigned int side, y;
	enum mh_error_code status;

	if (source == NULL || output == NULL || source->rgba == NULL ||
		output->rgba == NULL || source->rgba == output->rgba ||
		(format != MH_TEXTURE_RGB555 && format != MH_TEXTURE_ARGB4444))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR texture preparation");

	side = 0;
	status = mh_powervr_texture_size(
		source->width, source->height, &side, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (output->width != side || output->height != side)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incorrect PowerVR texture output dimensions");

	memset(output->rgba, 0, (size_t)side * side * 4);

	for (y = 0; y < source->height; y++)
		memcpy(output->rgba + (size_t)y * side * 4,
			source->rgba + (size_t)y * source->width * 4,
			(size_t)source->width * 4);

	return mh_image_texture_format(output, format, error_out);
}

int
mh_powervr_texture_unused(const struct mh_mesh *mesh, size_t texture)
{
	size_t i;

	/* Dynamic effects have no atlas and can gain draws after creation. */
	if (mesh->atlas == NULL || texture == 0 ||
		texture == mesh->environment_texture)
		return 0;

	for (i = 0; i < mesh->draw_count; i++)
		if (mesh->draws[i].texture == texture)
			return 0;

	return 1;
}

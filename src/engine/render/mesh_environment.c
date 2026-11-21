#include <stdlib.h>
#include <string.h>

#include "engine/render/mesh.h"
#include "interfaces/image/decoder.h"

enum mh_error_code
mh_mesh_set_environment(const struct mh_image_decoder_provider *decoder,
	struct mh_mesh *mesh, const unsigned char *data, size_t size,
	enum mh_texture_format format, struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_image *textures;
	unsigned char *formats;
	size_t count, retained, index;
	enum mh_error_code status;

	if (mesh->texture_count == 0 ||
		mesh->texture_count >= (size_t)-1 / sizeof(*textures)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture table for environment map");
	}

	status = mh_image_decode(
		decoder, data, size, NULL, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_texture_format(&image, format, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	index = mesh->environment_texture != 0 ? mesh->environment_texture
					: mesh->texture_count;
	count = mesh->texture_count + (mesh->environment_texture == 0);
	formats = (unsigned char *)calloc(count, 1);

	if (formats == NULL) {
		free(image.rgba);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining environment format");
	}

	retained = mesh->texture_format_count < count
		? mesh->texture_format_count
		: count;

	if (retained != 0)
		memcpy(formats, mesh->texture_formats, retained);

	formats[index] = (unsigned char)format;
	textures = mesh->textures;

	if (mesh->environment_texture == 0) {
		textures = (struct mh_image *)realloc(
			mesh->textures, count * sizeof(*textures));

		if (textures == NULL) {
			free(formats);
			free(image.rgba);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory adding environment map");
		}
	} else
		free(textures[index].rgba);

	textures[index] = image;
	mesh->environment_texture = index;
	mesh->texture_count = count;
	mesh->textures = textures;
	free(mesh->texture_formats);
	mesh->texture_formats = formats;
	mesh->texture_format_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

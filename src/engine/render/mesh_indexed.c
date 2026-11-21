#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/image/collection_load.h"
#include "engine/render/mesh.h"

static void
free_images(struct mh_indexed_image *images, size_t count)
{
	size_t i;

	if (images != NULL)
		for (i = 0; i < count; i++)
			free(images[i].indices);

	free(images);
}

enum mh_error_code
mh_mesh_load_indexed_textures(const struct mh_image_decoder_provider *decoder,
	struct mh_mesh *mesh, const struct mh_image_collection *collection,
	struct mh_error *error_out)
{
	struct mh_indexed_image *images;
	const struct mh_draw *draw;
	const char **names;
	size_t i, texture_index;
	enum mh_error_code status;

	if (mesh == NULL || collection == NULL || mesh->textures == NULL ||
		mesh->texture_count == 0 ||
		mesh->texture_count > (size_t)-1 / sizeof(*images) ||
		mesh->texture_count > (size_t)-1 / sizeof(*names) ||
		(mesh->draw_count != 0 && mesh->draws == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed mesh texture table");

	images = (struct mh_indexed_image *)calloc(
		mesh->texture_count, sizeof(*images));
	names = (const char **)calloc(mesh->texture_count, sizeof(*names));

	if (images == NULL || names == NULL) {
		free(images);
		free(names);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading indexed textures");
	}

	status = MH_ERROR_NONE;

	for (i = 0; i < mesh->draw_count; i++) {
		draw = &mesh->draws[i];
		texture_index = draw->texture;

		if (texture_index >= mesh->texture_count ||
			memchr(draw->texture_name, 0,
				sizeof(draw->texture_name)) == NULL) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid indexed texture binding");
			break;
		}

		if (draw->texture_name[0] == 0 || draw->environment)
			continue;

		if (names[texture_index] != NULL) {
			if (!mh_ascii_equal(
				names[texture_index], draw->texture_name)) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"conflicting indexed texture names");
				break;
			}

			continue;
		}

		names[texture_index] = draw->texture_name;

		status = mh_image_collection_load_indexed(decoder, collection,
			draw->texture_name, &images[texture_index], error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (images[texture_index].width !=
				mesh->textures[texture_index].width ||
			images[texture_index].height !=
				mesh->textures[texture_index].height) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"indexed texture dimensions differ from mesh UV source");
			break;
		}
	}

	free(names);

	if (status != MH_ERROR_NONE) {
		free_images(images, mesh->texture_count);
		return status;
	}

	free_images(mesh->indexed_textures, mesh->indexed_texture_count);
	mesh->indexed_textures = images;
	mesh->indexed_texture_count = mesh->texture_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

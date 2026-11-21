#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_effects.h"
#include "engine/catalog/halo_paths.h"
#include "engine/image/load.h"
#include "engine/render/software_scene_halo_assets.h"

#define HALO_IMAGE_LIMIT 256

static enum mh_error_code
load_image(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *relative, struct mh_indexed_image *output,
	struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(
		image_decoder, resources, root, relative, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Keep original dimensions: shipped glows are 150, 163 or 170 square.
	 */
	if (image.width == 0 || image.width > HALO_IMAGE_LIMIT ||
		image.height == 0 || image.height > HALO_IMAGE_LIMIT) {
		free(image.indices);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported software halo image dimensions");
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_software_halo_assets_free(struct mh_software_halo_assets *assets)
{
	unsigned int i;

	if (assets == NULL)
		return;

	for (i = 0; i < MH_HALO_TEXTURES; i++)
		free(assets->images[i].indices);

	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_software_halo_assets_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_halo_paths *paths,
	struct mh_software_halo_assets *output, struct mh_error *error_out)
{
	struct mh_software_halo_assets next;
	const char *path;
	unsigned int i;
	enum mh_error_code status;

	if (root == NULL || paths == NULL || output == NULL ||
		paths->count > MH_HALO_TEXTURES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo asset inputs");

	memset(&next, 0, sizeof(next));

	/* Load every Halo entry in order, without deduplication. */
	for (i = 0; i < paths->count; i++) {
		path = mh_halo_path(paths, i);
		status = path == NULL
			? mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software halo path list")
			: load_image(image_decoder, resources, root, path,
				&next.images[i], error_out);

		if (status != MH_ERROR_NONE) {
			mh_software_halo_assets_free(&next);
			return status;
		}
	}

	next.count = paths->count;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_lens_image_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software lens image inputs");

	/* Load the halo descriptor. */

	return load_image(image_decoder, resources, root,
		MH_ASSET_SOFTWARE_HALO, output, error_out);
}

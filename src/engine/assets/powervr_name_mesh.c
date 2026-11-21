#include <stdlib.h>
#include <string.h>

#include "engine/assets/name_mesh.h"
#include "engine/render/powervr_lamp_atlas.h"

#define LABEL_HEIGHT 2
#define LABEL_SIZE .8f

/* The shared page is changed only after all owned allocations succeed. */
enum mh_error_code
mh_powervr_name_mesh_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_atlas *atlas,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_name_mesh *output, struct mh_error *error_out)
{
	struct mh_name_images images;
	struct mh_name_mesh result;
	struct mh_name_label *label;
	unsigned int i, channel;
	enum mh_error_code status;

	if (output == NULL || atlas == NULL || atlas->count != 1 ||
		atlas->pages == NULL || palette == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR name mesh atlas");

	status = mh_name_images_create(
		font, profiles, count, active_mask, &images, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.profile = MH_RENDER_POWERVR;
	result.count = count;
	result.active_mask = active_mask;
	status = mh_name_mesh_allocate(&result, error_out);

	if (status == MH_ERROR_NONE) {
		result.mesh.atlas_images =
			(size_t *)calloc(1, sizeof(*result.mesh.atlas_images));
		result.mesh.material_rgb = (float *)calloc(
			result.mesh.vertex_count, 3 * sizeof(float));

		if (result.mesh.atlas_images == NULL ||
			result.mesh.material_rgb == NULL)
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory binding name atlas");
	}

	if (status == MH_ERROR_NONE) {
		result.mesh.atlas = atlas;
		result.mesh.material_rgb_count = result.mesh.vertex_count;

		for (i = 0; i < count; i++) {
			if (!(active_mask & (1U << i)))
				continue;

			label = &result.labels[i];
			label->height = LABEL_HEIGHT;
			label->size[0] = label->size[1] = LABEL_SIZE;

			for (channel = 0; channel < 3; channel++)
				label->color[channel] =
					images.images[i].palette[3 + channel];
		}

		status = mh_powervr_lamp_atlas_names(&images, palette,
			&atlas->pages[0], result.labels, error_out);
	}

	mh_name_images_free(&images);

	if (status != MH_ERROR_NONE) {
		mh_name_mesh_free(&result);
		return status;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

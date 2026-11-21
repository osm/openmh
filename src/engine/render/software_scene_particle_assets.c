#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_effects.h"
#include "engine/image/load.h"
#include "engine/render/software_scene_particle_assets.h"

struct animation_source {
	const char	*path;
	unsigned int	first, count, size;
};

void
mh_software_particle_assets_free(struct mh_software_particle_assets *assets)
{
	unsigned int group, frame;

	if (assets == NULL)
		return;

	for (group = 0; group < MH_PARTICLE_ANIMATIONS; group++)
		for (frame = 0; frame < MH_SOFTWARE_PARTICLE_FRAMES; frame++)
			free(assets->animations[group].frames[frame].indices);

	memset(assets, 0, sizeof(*assets));
}

static enum mh_error_code
load_frame(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct animation_source *source, unsigned int frame,
	struct mh_indexed_image *image, struct mh_error *error_out)
{
	char relative[128];
	enum mh_error_code status;

	sprintf(relative, source->path, source->first + frame);
	status = mh_image_load_indexed_from(
		image_decoder, resources, root, relative, image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (image->width != source->size || image->height != source->size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected software particle image dimensions");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_particle_assets_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_software_particle_assets *output, struct mh_error *error_out)
{
	/* In mh_particle_animation_id order. */
	static const struct animation_source sources[] = {
		{MH_ASSET_SOFTWARE_SMOKE_FORMAT, 21, 17, 64},
		{MH_ASSET_SOFTWARE_GRASS_FORMAT, 40, 17, 64},
		{MH_ASSET_SOFTWARE_GRAVEL_FORMAT, 20, 17, 64},
		{MH_ASSET_SOFTWARE_SPARK_FORMAT, 20, 20, 128}};

	struct mh_software_particle_assets next;
	unsigned int group, frame;
	enum mh_error_code status;

	if (root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software particle asset inputs");

	memset(&next, 0, sizeof(next));

	for (group = 0; group < MH_PARTICLE_ANIMATIONS; group++) {
		next.animations[group].count = sources[group].count;

		for (frame = 0; frame < sources[group].count; frame++) {
			status = load_frame(image_decoder, resources, root,
				&sources[group], frame,
				&next.animations[group].frames[frame],
				error_out);

			if (status != MH_ERROR_NONE) {
				mh_software_particle_assets_free(&next);
				return status;
			}
		}
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

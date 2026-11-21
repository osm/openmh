#include <stdlib.h>
#include <string.h>

#include "engine/render/powervr_lamp_atlas.h"
#include "engine/render/scene_internal.h"
#include "interfaces/image/decoder.h"

#define NO_ATLAS_PAGE ((size_t)-1)

static enum mh_error_code
prepare_atlas(const struct mh_image_decoder_provider *decoder,
	struct mh_atlas *atlas, const struct mh_file *glow,
	const struct mh_file *flare,
	unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_error *error_out)
{
	struct mh_indexed_image sources[2];
	enum mh_error_code status;

	memset(sources, 0, sizeof(sources));
	atlas->pages = (struct mh_image *)calloc(1, sizeof(*atlas->pages));
	atlas->formats = (unsigned char *)malloc(1);

	if (atlas->pages == NULL || atlas->formats == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating lamp atlas");

	atlas->count = 1;
	atlas->formats[0] = MH_TEXTURE_ARGB4444;
	atlas->pages[0].width = atlas->pages[0].height =
		MH_POWERVR_LAMP_ATLAS_SIZE;
	atlas->pages[0].rgba =
		(unsigned char *)malloc((size_t)MH_POWERVR_LAMP_ATLAS_SIZE *
			MH_POWERVR_LAMP_ATLAS_SIZE * 4);

	if (atlas->pages[0].rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for lamp atlas pixels");

	status = mh_image_decode_indexed(
		decoder, glow->data, glow->size, &sources[0], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_image_decode_indexed(decoder, flare->data,
			flare->size, &sources[1], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_powervr_lamp_atlas_prepare(
			&sources[0], &sources[1], &atlas->pages[0], error_out);

	if (status == MH_ERROR_NONE)
		memcpy(palette, sources[0].palette, sizeof(sources[0].palette));

	free(sources[0].indices);
	free(sources[1].indices);

	return status;
}

enum mh_error_code
mh_race_scene_load_lamp_atlas(struct mh_race_scene *scene,
	const struct mh_file *glow, const struct mh_file *flare,
	struct mh_error *error_out)
{
	struct mh_mesh *shadow, *lamps;
	size_t texture;
	struct mh_image *images;
	enum mh_error_code status;

	status = prepare_atlas(scene->image_decoder, &scene->assets.lamp_atlas,
		glow, flare, scene->assets.lamp_palette, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	shadow = &scene->visuals.shadow_mesh;
	images = (struct mh_image *)realloc(
		shadow->textures, 2 * sizeof(*images));

	if (images == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory binding shadow atlas");

	shadow->textures = images;
	memset(&images[1], 0, sizeof(images[1]));
	shadow->texture_count = 2;
	shadow->atlas_images =
		(size_t *)malloc(2 * sizeof(*shadow->atlas_images));

	if (shadow->atlas_images == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for shadow atlas bindings");

	shadow->atlas_images[0] = NO_ATLAS_PAGE;
	shadow->atlas_images[1] = 0;
	shadow->atlas = &scene->assets.lamp_atlas;
	shadow->draws[0].texture = 1;

	lamps = &scene->visuals.lamps.mesh;
	lamps->atlas_images = (size_t *)calloc(
		lamps->texture_count, sizeof(*lamps->atlas_images));
	lamps->material_rgb = (float *)calloc(
		lamps->vertex_count, 3 * sizeof(*lamps->material_rgb));

	if (lamps->atlas_images == NULL || lamps->material_rgb == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory binding lamp atlas");

	lamps->material_rgb_count = lamps->vertex_count;
	lamps->atlas = &scene->assets.lamp_atlas;

	for (texture = 0; texture < lamps->texture_count; texture++) {

		free(lamps->textures[texture].rgba);
		memset(&lamps->textures[texture], 0,
			sizeof(lamps->textures[texture]));
	}

	scene->visuals.lamps.profile = MH_RENDER_POWERVR;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

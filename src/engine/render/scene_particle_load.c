#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_effects.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/render/scene.h"

struct animation_frames {
	const char	*path;
	unsigned int	animation, count, size;
};

static void
copy_frame(struct mh_image *atlas, const struct mh_image *image,
	unsigned int tile_size, const float uv[8])
{
	unsigned char *target;
	unsigned int x, y, ox, oy;

	ox = (unsigned int)(uv[0] * MH_PARTICLE_ATLAS_SIZE) - 1;
	oy = (unsigned int)(uv[1] * MH_PARTICLE_ATLAS_SIZE) - 1;

	for (y = 0; y < tile_size; y++)
		for (x = 0; x < tile_size; x++) {
			target = atlas->rgba +
				((oy + y) * MH_PARTICLE_ATLAS_SIZE + ox + x) *
					4;
			memcpy(target, image->rgba + (y * tile_size + x) * 4,
				3);
			target[3] = 255;
		}
}

/* Preserve RGB, replace alpha by mean brightness. */
static enum mh_error_code
prepare_powervr(struct mh_mesh *mesh, struct mh_error *error_out)
{
	unsigned char *pixel;
	size_t i, pixel_count;

	mesh->texture_formats = (unsigned char *)malloc(1);
	mesh->material_rgb =
		(float *)calloc(mesh->vertex_count * 3, sizeof(float));

	if (mesh->texture_formats == NULL || mesh->material_rgb == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing PowerVR particles");

	mesh->texture_format_count = 1;
	mesh->texture_formats[0] = MH_TEXTURE_ARGB4444;
	mesh->material_rgb_count = mesh->vertex_count;
	pixel_count =
		(size_t)mesh->textures[0].width * mesh->textures[0].height;

	for (i = 0; i < pixel_count; i++) {
		pixel = mesh->textures[0].rgba + i * 4;
		pixel[3] =
			(unsigned char)((pixel[0] + pixel[1] + pixel[2]) / 3);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_load_particles_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	static const struct animation_frames frames[] = {
		{MH_ASSET_HARDWARE_SMOKE_FORMAT, MH_PARTICLE_SMOKE, 9, 32},
		{MH_ASSET_HARDWARE_GRASS_FORMAT, MH_PARTICLE_GRASS, 9, 32},
		{MH_ASSET_HARDWARE_SPARK_FORMAT, MH_PARTICLE_SPARK, 6, 64}};

	struct mh_mesh mesh;
	struct mh_image image;
	struct mh_image_palette palette;
	struct mh_image_decode_options options;
	char relative[128];
	unsigned int group, frame, count, tile_size;
	float uv[8];
	enum mh_error_code status;

	if (scene == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing particle assets");

	status = mh_particle_mesh_create(MH_RACE_MAX_CARS, &mesh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (group = 0; group < sizeof(frames) / sizeof(frames[0]) &&
		status == MH_ERROR_NONE;
		group++) {
		count = frames[group].count;
		tile_size = frames[group].size;
		palette.count = 0;

		for (frame = 0; frame < count && status == MH_ERROR_NONE;
			frame++) {
			sprintf(relative, frames[group].path, frame + 1);
			memset(&image, 0, sizeof(image));
			options.alpha = MH_IMAGE_ALPHA_FILE;

			/*
			 * A true-color replacement already contains the
			 * selected colors.
			 */
			options.palette = frame != 0 && palette.count != 0
				? &palette
				: NULL;
			status = mh_image_load_from(image_decoder, resources,
				root, relative, &options, &image,
				frame == 0 ? &palette : NULL, error_out);

			if (status != MH_ERROR_NONE) {
				free(image.rgba);
				break;
			}

			if (image.width != tile_size ||
				image.height != tile_size)
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"unexpected original particle image dimensions");

			if (status == MH_ERROR_NONE)
				status = mh_particle_frame_uv(
					frames[group].animation, frame, uv,
					error_out);

			if (status == MH_ERROR_NONE)
				copy_frame(&mesh.textures[0], &image, tile_size,
					uv);

			free(image.rgba);
		}
	}

	if (status == MH_ERROR_NONE &&
		scene->assets.profile == MH_RENDER_POWERVR)
		status = prepare_powervr(&mesh, error_out);

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&mesh);
		return status;
	}

	scene->visuals.particle_mesh = mesh;
	mh_race_scene_reset_particles(scene);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include "backends/render/sdl_gpu_powervr_texture.h"
#include "engine/render/powervr_texture.h"

int
mh_gpu_powervr_image_size_prepare(struct mh_gpu_image *image)
{
	const struct mh_image *source;
	struct mh_error error;
	unsigned int side;

	source = image->source;
	image->width = source->width;
	image->height = source->height;

	/*
	 * Native untextured draws sample a white pixel, not an original upload.
	 */
	if (image->format == 0 && source->width == 1 && source->height == 1 &&
		source->rgba[0] == 255 && source->rgba[1] == 255 &&
		source->rgba[2] == 255 && source->rgba[3] == 255)
		return 1;

	if (image->format != MH_TEXTURE_RGB555 &&
		image->format != MH_TEXTURE_ARGB4444)
		return SDL_SetError(
			"PowerVR requires RGB555 or ARGB4444 texture preparation");

	if (mh_powervr_texture_size(source->width, source->height, &side,
		&error) != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	image->width = image->height = side;

	return 1;
}

int
mh_gpu_powervr_sky_uv_scale(const struct mh_gpu_renderer *gpu,
	const struct mh_gpu_mesh *model, struct mh_vertex *vertices)
{
	const struct mh_mesh *mesh;
	const struct mh_draw *draw;
	const struct mh_gpu_image *image;
	size_t i, j, image_index;
	float scale[2];

	mesh = model->mesh;

	for (i = 0; i < mesh->draw_count; i++) {
		draw = &mesh->draws[i];

		if (draw->texture >= mesh->texture_count ||
			draw->first > mesh->vertex_count ||
			draw->count > mesh->vertex_count - draw->first)
			return SDL_SetError("invalid PowerVR sky draw");

		image_index = model->images[draw->texture];

		if (image_index >= gpu->image_count)
			return SDL_SetError("missing PowerVR sky texture");

		image = &gpu->images[image_index];
		scale[0] = (float)image->source->width / image->width;
		scale[1] = (float)image->source->height / image->height;

		for (j = draw->first; j < draw->first + draw->count; j++) {
			vertices[j].uv[0] = mesh->vertices[j].uv[0] * scale[0];
			vertices[j].uv[1] = mesh->vertices[j].uv[1] * scale[1];
		}
	}

	return 1;
}

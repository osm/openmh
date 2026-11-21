#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/atmosphere_internal.h"
#include "engine/render/atmosphere_sky.h"
#include "interfaces/image/decoder.h"

#define SKY_REFERENCE_HEIGHT 512
#define SKY_TEXTURE_WIDTH 256
#define SKY_OPACITY (127.0f / 255)
/* Retain reciprocal W .0001 for ordinary/black sky. */
#define SKY_PROJECTION_W 10000.0f

static enum mh_error_code
resize_panorama(struct mh_image *image, struct mh_error *error_out)
{
	unsigned char *pixels;
	unsigned int x, y, source_x;
	size_t source, destination;
	float scale;

	if (image->width == SKY_TEXTURE_WIDTH)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	pixels = malloc((size_t)SKY_TEXTURE_WIDTH * image->height * 4);

	if (pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resizing sky panorama");

	/* Centered nearest sample, same height. */
	scale = (float)image->width / SKY_TEXTURE_WIDTH;

	for (y = 0; y < image->height; y++) {
		for (x = 0; x < SKY_TEXTURE_WIDTH; x++) {
			source_x = (unsigned int)((x + 0.5) * scale);
			source = ((size_t)y * image->width + source_x) * 4;
			destination = ((size_t)y * SKY_TEXTURE_WIDTH + x) * 4;
			memcpy(pixels + destination, image->rgba + source, 4);
		}
	}

	free(image->rgba);
	image->rgba = pixels;
	image->width = SKY_TEXTURE_WIDTH;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sky_mesh_create(const struct mh_image_decoder_provider *image_decoder,
	const unsigned char *data, size_t size, struct mh_mesh *output,
	struct mh_error *error_out)
{
	struct mh_mesh mesh;
	enum mh_error_code status;
	unsigned int quad;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing sky mesh output");

	status = mh_atmosphere_mesh_allocate(
		SKY_VERTICES, SKY_QUADS, SKY_TEXTURES, &mesh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode(image_decoder, data, size, NULL,
		&mesh.textures[1], NULL, error_out);

	if (status == MH_ERROR_NONE &&
		(mesh.textures[1].height < 2 ||
			mesh.textures[1].height > SKY_TEXTURE_HEIGHT_LIMIT))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sky texture height");

	if (status == MH_ERROR_NONE)
		status = resize_panorama(&mesh.textures[1], error_out);

	if (status == MH_ERROR_NONE) {
		mesh.textures[0].width = mesh.textures[0].height = 1;
		mesh.textures[0].rgba = (unsigned char *)malloc(4);

		if (mesh.textures[0].rgba == NULL)
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory for sky fill");
		else
			memset(mesh.textures[0].rgba, 255, 4);
	}

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&mesh);
		return status;
	}

	for (quad = 0; quad < SKY_QUADS; quad++) {
		mesh.draws[quad].first = quad * QUAD_VERTICES;
		mesh.draws[quad].count = QUAD_VERTICES;
		mesh.draws[quad].prelit = 1;
	}

	mesh.draws[0].texture = 1;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sky_mesh_black(struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_effect_quad quad;
	unsigned int vertex;

	if (mesh == NULL || mesh->vertices == NULL || mesh->draws == NULL ||
		mesh->vertex_count != SKY_VERTICES ||
		mesh->draw_count != SKY_QUADS ||
		mesh->texture_count < SKY_TEXTURES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid black sky mesh");

	memset(&quad, 0, sizeof(quad));
	quad.xy[1][0] = quad.xy[2][0] = 1;
	quad.xy[2][1] = quad.xy[3][1] = 1;
	mh_atmosphere_quad_vertices(mesh->vertices, &quad, 1, 1, SKY_DEPTH, 1);

	for (vertex = 0; vertex < QUAD_VERTICES; vertex++)
		mesh->vertices[vertex].projection_w = SKY_PROJECTION_W;

	mesh->draws[0].texture = 0;
	mesh->draws[0].count = QUAD_VERTICES;
	mesh->draws[1].count = mesh->draws[2].count = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sky_mesh_weather(struct mh_mesh *mesh,
	const struct mh_weather_camera *camera, struct mh_error *error_out)
{
	static const unsigned int corners[QUAD_VERTICES] = {0, 1, 2, 0, 2, 3};
	struct mh_weather_vertex sky[WEATHER_QUADS * 4];
	struct mh_vertex staged_vertices[SKY_VERTICES];
	const struct mh_weather_vertex *source;
	struct mh_vertex *vertex;
	enum mh_error_code status;
	unsigned int quad_count, index, channel, shift;

	if (mesh == NULL || mesh->vertices == NULL || mesh->draws == NULL ||
		mesh->vertex_count != SKY_VERTICES ||
		mesh->draw_count != SKY_QUADS ||
		mesh->environment_texture == 0 ||
		mesh->environment_texture >= mesh->texture_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid special sky mesh");

	status = mh_weather_sky(camera, sky, &quad_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(staged_vertices, 0, sizeof(staged_vertices));

	for (index = 0; index < quad_count * QUAD_VERTICES; index++) {
		source = &sky[index / QUAD_VERTICES * 4 +
			corners[index % QUAD_VERTICES]];
		vertex = &staged_vertices[index];
		vertex->position[0] = source->xy[0] * 2 / camera->width - 1;
		vertex->position[1] = 1 - source->xy[1] * 2 / camera->height;
		vertex->position[2] = source->depth;
		vertex->opacity = (float)((source->color >> 24) & 255) / 255;
		vertex->projection_w =
			(float)(1 / (double)source->reciprocal_w);
		memcpy(vertex->uv, source->uv, sizeof(vertex->uv));

		for (channel = 0; channel < 3; channel++) {
			shift = 16 - channel * 8;
			vertex->color[channel] =
				(float)((source->color >> shift) & 255) / 255;
		}
	}

	memcpy(mesh->vertices, staged_vertices, sizeof(staged_vertices));
	mesh->draws[0].texture = mesh->environment_texture;
	mesh->draws[1].texture = 0;
	mesh->draws[0].count = mesh->draws[1].count =
		quad_count == 2 ? QUAD_VERTICES : 0;
	mesh->draws[2].count = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_sky(struct mh_mesh *mesh, struct mh_sky_state *state,
	const struct mh_track *track, const float world_to_clip[16],
	const float world_to_view[16], unsigned int width, unsigned int height,
	int show_image, struct mh_error *error_out)
{
	/* Shared hardware fill, without bit replication. */
	static const unsigned char fill_masks[3] = {0xf8, 0xfc, 0xf8};
	struct mh_sky_camera camera;
	struct mh_effect_quad quads[SKY_QUADS];
	const unsigned char *fill;
	struct mh_vertex *vertex;
	enum mh_error_code status;
	unsigned int quad, component;

	if (mesh == NULL || track == NULL || mesh->vertices == NULL ||
		mesh->draws == NULL || mesh->textures == NULL ||
		mesh->vertex_count != SKY_VERTICES ||
		mesh->draw_count != SKY_QUADS ||
		mesh->texture_count < SKY_TEXTURES ||
		!mh_atmosphere_camera_valid(world_to_clip, width, height) ||
		!mh_atmosphere_camera_valid(world_to_view, width, height))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sky mesh camera");

	camera.width = (float)width;
	camera.height = (float)height;
	camera.focal = (float)sqrt((double)world_to_clip[0] * world_to_clip[0] +
		(double)world_to_clip[4] * world_to_clip[4] +
		(double)world_to_clip[8] * world_to_clip[8]);

	for (component = 0; component < 3; component++)
		camera.forward[component] = -world_to_view[component * 4 + 2];

	camera.roll_sine = world_to_view[4];
	camera.horizon = (float)track->horizon;
	camera.image_height = show_image ? (float)mesh->textures[1].height : 0;

	/* Scale panorama height to the viewport. */
	camera.image_scale = (float)((double)height / SKY_REFERENCE_HEIGHT);
	status = mh_sky_quads(state, &camera, quads, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mesh->draws[0].texture = show_image ? 1 : 0;

	for (quad = 0; quad < SKY_QUADS; quad++) {
		mesh->draws[quad].count =
			quad == 0 && !show_image ? 0 : QUAD_VERTICES;

		fill = quad == 1 ? track->sky_top : track->sky_bottom;

		for (component = 0; component < 3; component++)
			quads[quad].color[component] = quad == 0
				? 255
				: fill[component] & fill_masks[component];

		mh_atmosphere_quad_vertices(
			mesh->vertices + quad * QUAD_VERTICES, &quads[quad],
			width, height, SKY_DEPTH, 1);

		for (component = 0; component < QUAD_VERTICES; component++) {
			vertex = &mesh->vertices[quad * QUAD_VERTICES +
				component];
			vertex->opacity = SKY_OPACITY;
			vertex->projection_w = SKY_PROJECTION_W;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sky_mesh_update(struct mh_mesh *mesh, struct mh_sky_state *state,
	const struct mh_track *track, const float world_to_clip[16],
	const float world_to_view[16], unsigned int width, unsigned int height,
	struct mh_error *error_out)
{
	return update_sky(mesh, state, track, world_to_clip, world_to_view,
		width, height, 1, error_out);
}

enum mh_error_code
mh_sky_mesh_plain(struct mh_mesh *mesh, struct mh_sky_state *state,
	const struct mh_track *track, const float world_to_clip[16],
	const float world_to_view[16], unsigned int width, unsigned int height,
	struct mh_error *error_out)
{
	return update_sky(mesh, state, track, world_to_clip, world_to_view,
		width, height, 0, error_out);
}

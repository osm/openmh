#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/atmosphere_internal.h"

enum mh_error_code
mh_atmosphere_mesh_allocate(size_t vertex_count, size_t draw_count,
	size_t texture_count, struct mh_mesh *mesh, struct mh_error *error_out)
{
	memset(mesh, 0, sizeof(*mesh));

	mesh->vertex_count = vertex_count;
	mesh->draw_count = draw_count;
	mesh->texture_count = texture_count;
	mesh->vertices = (struct mh_vertex *)calloc(
		vertex_count, sizeof(*mesh->vertices));
	mesh->draws =
		(struct mh_draw *)calloc(draw_count, sizeof(*mesh->draws));
	mesh->textures = (struct mh_image *)calloc(
		texture_count, sizeof(*mesh->textures));

	if (mesh->vertices == NULL || mesh->draws == NULL ||
		mesh->textures == NULL) {
		/* Free accepts an absent texture table only with zero count. */
		if (mesh->textures == NULL)
			mesh->texture_count = 0;

		mh_mesh_free(mesh);

		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for atmosphere mesh");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_atmosphere_quad_vertices(struct mh_vertex *output,
	const struct mh_effect_quad *quad, unsigned int width,
	unsigned int height, float depth, float uv_scale)
{
	static const unsigned int quad_corners[QUAD_VERTICES] = {
		0, 1, 2, 0, 2, 3};

	struct mh_vertex *vertex;
	unsigned int index, component, corner;

	for (index = 0; index < QUAD_VERTICES; index++) {
		corner = quad_corners[index];
		vertex = &output[index];
		memset(vertex, 0, sizeof(*vertex));
		vertex->position[0] = quad->xy[corner][0] * 2 / width - 1;
		vertex->position[1] = 1 - quad->xy[corner][1] * 2 / height;
		vertex->position[2] = depth;

		for (component = 0; component < 2; component++)
			vertex->uv[component] =
				quad->uv[corner][component] * uv_scale;

		for (component = 0; component < 3; component++)
			vertex->color[component] =
				(float)(unsigned int)quad->color[component] /
				255;
	}
}

#if MH_RENDER_BUILD_POWERVR
void
mh_atmosphere_powervr_quad(struct mh_vertex *output, float *colors,
	const struct mh_effect_quad *quad, unsigned int width,
	unsigned int height)
{
	unsigned int vertex;

	memset(output, 0, QUAD_VERTICES * sizeof(*output));
	memset(colors, 0, QUAD_VERTICES * 3 * sizeof(*colors));

	/*
	 * Nonpositive reciprocal depth lies behind the cleared SGL depth plane.
	 */
	if (!(quad->reciprocal_w > 0 && quad->reciprocal_w <= FLT_MAX))
		return;

	/*
	 * Fixed Z does not participate in SGL depth; keep native clipping
	 * valid.
	 */
	mh_atmosphere_quad_vertices(output, quad, width, height, .5f, 1);

	for (vertex = 0; vertex < QUAD_VERTICES; vertex++) {
		output[vertex].projection_w = 1 / quad->reciprocal_w;
		memcpy(colors + vertex * 3, quad->color, 3 * sizeof(*colors));
	}
}
#endif

int
mh_atmosphere_camera_valid(
	const float matrix[16], unsigned int width, unsigned int height)
{
	unsigned int element;

	if (matrix == NULL || width < 1 || width > VIEWPORT_LIMIT ||
		height < 1 || height > VIEWPORT_LIMIT)
		return 0;

	for (element = 0; element < 16; element++)
		if (!(matrix[element] >= -MATRIX_LIMIT &&
			matrix[element] <= MATRIX_LIMIT))
			return 0;

	return 1;
}

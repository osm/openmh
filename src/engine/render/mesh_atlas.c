#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/mesh.h"

#define NO_ATLAS_PAGE ((size_t)-1)
#define SOURCE_UV_LIMIT 256

const struct mh_image *
mh_mesh_texture(const struct mh_mesh *mesh, size_t texture_index)
{
	size_t page;

	if (mesh == NULL || mesh->textures == NULL ||
		texture_index >= mesh->texture_count)
		return NULL;

	if (mesh->atlas != NULL) {
		if (mesh->atlas_images == NULL)
			return NULL;

		page = mesh->atlas_images[texture_index];

		if (page != NO_ATLAS_PAGE) {
			if (page >= mesh->atlas->count ||
				mesh->atlas->pages == NULL)
				return NULL;

			return &mesh->atlas->pages[page];
		}
	}

	return &mesh->textures[texture_index];
}

unsigned int
mh_mesh_texture_format(const struct mh_mesh *mesh, size_t texture_index)
{
	size_t page;

	if (mh_mesh_texture(mesh, texture_index) == NULL)
		return 0;

	if (mesh->atlas != NULL) {
		page = mesh->atlas_images[texture_index];

		if (page != NO_ATLAS_PAGE)
			return mesh->atlas->formats == NULL
				? 0
				: mesh->atlas->formats[page];
	}

	return texture_index < mesh->texture_format_count
		? mesh->texture_formats[texture_index]
		: 0;
}

static enum mh_error_code
recover_source_uv(const float normalized[2], const struct mh_image *image,
	float source_uv[2], struct mh_error *error_out)
{
	unsigned int axis, dimension;
	double pixel_coordinate;

	for (axis = 0; axis < 2; axis++) {
		dimension = axis == 0 ? image->width : image->height;
		pixel_coordinate = (double)normalized[axis] * dimension;

		if (!(pixel_coordinate >= 0 &&
			pixel_coordinate < SOURCE_UV_LIMIT)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid original atlas UV byte");
		}

		/*
		 * Mesh builders normalized (source byte + .5).
		 * Recover that exact center, then require its
		 * original normalization to match; arbitrary
		 * inspection UVs must not be reinterpreted.
		 */
		source_uv[axis] = (float)((unsigned int)pixel_coordinate + .5);

		if ((float)((double)source_uv[axis] / dimension) !=
			normalized[axis]) {
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"atlas binding requires original byte UVs");
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_bind_atlas(struct mh_mesh *mesh, const struct mh_atlas_map *remap,
	const struct mh_atlas *atlas, struct mh_error *error_out)
{
	struct mh_atlas_map_entry entry;
	const struct mh_draw *draw;
	const struct mh_image *image;
	size_t *page_indices;
	unsigned char *visited_vertices;
	float *mapped_uv, source_uv[2];
	enum mh_error_code status;
	size_t i, vertex, texture_index;

	if (mesh == NULL || remap == NULL || atlas == NULL ||
		atlas->pages == NULL || atlas->count != remap->pages ||
		mesh->textures == NULL || mesh->texture_count == 0 ||
		mesh->vertices == NULL || mesh->vertex_count == 0 ||
		(mesh->draw_count != 0 && mesh->draws == NULL) ||
		mesh->texture_count > (size_t)-1 / sizeof(*page_indices) ||
		mesh->vertex_count > (size_t)-1 / 2 / sizeof(*mapped_uv))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mesh atlas binding");

	page_indices =
		(size_t *)malloc(mesh->texture_count * sizeof(*page_indices));
	mapped_uv =
		(float *)malloc(mesh->vertex_count * 2 * sizeof(*mapped_uv));
	visited_vertices = (unsigned char *)calloc(mesh->vertex_count, 1);

	if (page_indices == NULL || mapped_uv == NULL ||
		visited_vertices == NULL) {
		free(page_indices);
		free(mapped_uv);
		free(visited_vertices);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory binding mesh atlas");
	}

	for (i = 0; i < mesh->texture_count; i++)
		page_indices[i] = NO_ATLAS_PAGE;

	for (i = 0; i < mesh->vertex_count; i++)
		memcpy(mapped_uv + i * 2, mesh->vertices[i].uv,
			2 * sizeof(*mapped_uv));

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; status == MH_ERROR_NONE && i < mesh->draw_count; i++) {
		draw = &mesh->draws[i];
		texture_index = draw->texture;

		if (texture_index >= mesh->texture_count ||
			draw->first > mesh->vertex_count ||
			draw->count > mesh->vertex_count - draw->first ||
			memchr(draw->texture_name, 0,
				sizeof(draw->texture_name)) == NULL) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid atlas material range");
			break;
		}

		for (vertex = draw->first; vertex < draw->first + draw->count;
			vertex++) {
			if (visited_vertices[vertex]) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"overlapping atlas material ranges");
				break;
			}

			visited_vertices[vertex] = 1;
		}

		if (status != MH_ERROR_NONE)
			break;

		if (draw->texture_name[0] == 0)
			continue;

		status = mh_atlas_map_find(
			remap, draw->texture_name, &entry, error_out);

		if (status != MH_ERROR_NONE)
			break;

		image = &mesh->textures[texture_index];

		if (entry.page >= atlas->count ||
			atlas->pages[entry.page].rgba == NULL ||
			atlas->pages[entry.page].width != remap->width ||
			atlas->pages[entry.page].height != remap->height ||
			image->width != entry.source_width ||
			image->height != entry.source_height ||
			(page_indices[texture_index] != NO_ATLAS_PAGE &&
				page_indices[texture_index] != entry.page) ||
			(mesh->environment_texture != 0 &&
				texture_index == mesh->environment_texture)) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"inconsistent mesh atlas image");
			break;
		}

		page_indices[texture_index] = (size_t)entry.page;

		for (vertex = draw->first; status == MH_ERROR_NONE &&
			vertex < draw->first + draw->count;
			vertex++) {
			status = recover_source_uv(mesh->vertices[vertex].uv,
				image, source_uv, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_atlas_uv(&entry,
					atlas->pages[entry.page].width,
					atlas->pages[entry.page].height,
					atlas->border, source_uv,
					mapped_uv + vertex * 2, error_out);
		}
	}

	free(visited_vertices);

	if (status == MH_ERROR_NONE) {
		free(mesh->atlas_images);
		free(mesh->atlas_uv);
		mesh->atlas_images = page_indices;
		mesh->atlas_uv = mapped_uv;
		mesh->atlas = atlas;
	} else {
		free(page_indices);
		free(mapped_uv);
	}

	return status;
}

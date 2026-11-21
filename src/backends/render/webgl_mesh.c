#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_mesh.h"
#include "engine/render/powervr_material.h"

static enum mh_error_code
sky_uv(const struct mh_webgl_mesh *mesh, struct mh_vertex *vertices,
	struct mh_error *error_out)
{
	const struct mh_mesh *source;
	const struct mh_draw *draw;
	const struct mh_webgl_image *image;
	float scale[2];
	size_t i, j, index;

	source = mesh->source;

	for (i = 0; i < source->draw_count; i++) {
		draw = &source->draws[i];

		if (draw->texture >= source->texture_count ||
			draw->first > source->vertex_count ||
			draw->count > source->vertex_count - draw->first)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid PowerVR sky draw");

		index = mesh->images[draw->texture];

		if (index >= mesh->pool->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing PowerVR sky texture");

		image = &mesh->pool->items[index];
		scale[0] = (float)image->source->width / image->texture.width;
		scale[1] = (float)image->source->height / image->texture.height;

		for (j = draw->first; j < draw->first + draw->count; j++) {
			vertices[j].uv[0] =
				source->vertices[j].uv[0] * scale[0];
			vertices[j].uv[1] =
				source->vertices[j].uv[1] * scale[1];
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_vertices(struct mh_webgl_mesh *mesh, const struct mh_vertex *vertices,
	enum mh_scene_effect effect, struct mh_error *error_out)
{
	struct mh_vertex *encoded, *previous;
	size_t i;
	int converted;
	enum mh_error_code status;

	converted = mesh->pool->powervr &&
		(effect == MH_SCENE_EFFECT_SKY ||
			effect == MH_SCENE_EFFECT_PARTICLES ||
			effect == MH_SCENE_EFFECT_HALOS);

	/* Fallible conversion must not overwrite an earlier pending update. */
	if (converted && mesh->encoding == NULL) {
		mesh->encoding = malloc(mesh->vertex_count * sizeof(*encoded));

		if (mesh->encoding == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot stage WebGL effect conversion");
	}

	encoded = converted ? mesh->encoding : mesh->upload;

	memcpy(encoded, vertices, mesh->geometry.count * sizeof(*vertices));

	if (mesh->source->atlas_uv != NULL)
		for (i = 0; i < mesh->geometry.count; i++)
			memcpy(encoded[i].uv, mesh->source->atlas_uv + i * 2,
				2 * sizeof(float));

	status = MH_ERROR_NONE;

	if (MH_RENDER_BUILD_POWERVR && converted) {
		status = effect == MH_SCENE_EFFECT_SKY
			? sky_uv(mesh, encoded, error_out)
			: mh_powervr_effect_colors_prepare(
				mesh->source, encoded, effect, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		previous = mesh->upload;
		mesh->upload = encoded;
		mesh->encoding = previous;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_mesh_update(struct mh_webgl_mesh *mesh,
	const struct mh_vertex *vertices, enum mh_scene_effect effect, int lit,
	struct mh_error *error_out)
{
	float *raw;
	size_t i, count;
	enum mh_error_code status;

	if (mesh == NULL || mesh->source == NULL || mesh->pool == NULL ||
		mesh->source->vertices == NULL ||
		mesh->source->vertex_count != mesh->geometry.count ||
		effect < MH_SCENE_EFFECT_SKY ||
		effect > MH_SCENE_EFFECT_SHADOWS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL mesh update");

	count = mesh->geometry.count;

	if (mesh->source->material_rgb != NULL &&
		mesh->source->material_rgb_count != count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid raw material color count");

	raw = mesh->material_rgb;

	if (mesh->source->material_rgb != NULL && raw == NULL) {
		raw = malloc(count * 3 * sizeof(float));

		if (raw == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot retain WebGL material colors");
	}

	if (vertices == NULL)
		vertices = mesh->source->vertices;

	status = encode_vertices(mesh, vertices, effect, error_out);

	if (status != MH_ERROR_NONE) {
		if (raw != mesh->material_rgb)
			free(raw);

		return status;
	}

	for (i = 0; i < count; i++) {
		memcpy(mesh->positions + i * 3, vertices[i].position,
			3 * sizeof(float));
		memcpy(mesh->colors + i * 3, vertices[i].color,
			3 * sizeof(float));
		mesh->projection_w[i] = vertices[i].projection_w;
	}

	if (mesh->source->material_rgb != NULL)
		memcpy(raw, mesh->source->material_rgb,
			count * 3 * sizeof(float));
	else {
		free(raw);
		raw = NULL;
	}

	mesh->material_rgb = raw;
	mesh->effect = effect;
	mesh->lit = lit;
	mesh->vertices_pending = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_mesh_create(struct mh_webgl_mesh *output, const struct mh_mesh *source,
	const struct mh_webgl_images *pool, size_t model,
	struct mh_error *error_out)
{
	struct mh_webgl_mesh mesh;
	enum mh_error_code status;
	size_t count;

	if (output == NULL || source == NULL || pool == NULL ||
		model >= pool->mesh_count || source->vertex_count == 0 ||
		source->vertex_count % 3 != 0 ||
		source->vertex_count > INT_MAX / sizeof(struct mh_vertex))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL mesh source");

	if (output->source != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL mesh is already created");

	memset(&mesh, 0, sizeof(mesh));
	mesh.source = source;
	mesh.pool = pool;
	mesh.images = pool->bindings[model];
	count = source->vertex_count;
	mesh.vertex_count = count;
	mesh.upload = malloc(count * sizeof(*mesh.upload));
	mesh.positions = malloc(count * 3 * sizeof(float));
	mesh.colors = malloc(count * 3 * sizeof(float));
	mesh.projection_w = malloc(count * sizeof(float));

	if (mesh.upload == NULL || mesh.positions == NULL ||
		mesh.colors == NULL || mesh.projection_w == NULL)
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL mesh attributes");
	else
		status = mh_webgl_geometry_create(
			&mesh.geometry, count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_mesh_update(
			&mesh, NULL, MH_SCENE_EFFECT_OPAQUE, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_meshes_upload(&mesh, 1, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_mesh_destroy(&mesh);
		return status;
	}

	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_mesh_restore(struct mh_webgl_mesh *mesh, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t count;

	count = mesh->vertex_count;

	if (mesh->source->vertex_count != count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"WebGL mesh capacity changed during context loss");

	status = mh_webgl_geometry_create(&mesh->geometry, count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_geometry_vertices(
			&mesh->geometry, mesh->upload, count, error_out);

	if (status == MH_ERROR_NONE && mesh->glenz_coordinates != NULL)
		status = mh_webgl_geometry_glenz(&mesh->geometry,
			mesh->glenz_coordinates, count, error_out);

	if (status == MH_ERROR_NONE)
		mesh->vertices_pending = mesh->glenz_pending = 0;

	return status;
}

void
mh_webgl_mesh_destroy(struct mh_webgl_mesh *mesh)
{
	if (mesh == NULL)
		return;

	mh_webgl_geometry_destroy(&mesh->geometry);
	free(mesh->upload);
	free(mesh->encoding);
	free(mesh->positions);
	free(mesh->colors);

	if (MH_RENDER_BUILD_OPENMH)
		mh_openmh_light_buffer_free(&mesh->openmh);

	free(mesh->projection_w);
	free(mesh->material_rgb);
	free(mesh->glenz_coordinates);
	memset(mesh, 0, sizeof(*mesh));
}

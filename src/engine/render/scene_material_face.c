#include <string.h>

#include "engine/render/face_vertex_color.h"
#include "engine/render/glenz.h"
#include "engine/render/powervr_environment.h"
#include "engine/render/powervr_face.h"
#include "engine/render/powervr_material.h"
#include "engine/render/powervr_shadow.h"
#include "engine/render/scene_material_face.h"
#include "engine/render/skid_face.h"

#define COLOR_NORMALIZER 255.0f

struct face_material {
	enum mh_scene_pass	pass;
	size_t			group;
	unsigned int		corners;
	int	shadow, skid, unavailable, environment, smooth, glenz;
};

static enum mh_error_code
project_corners(const struct mh_material_face_source *source, size_t first,
	unsigned int corner_count, const float camera_matrix[16],
	const struct mh_projection *projection, int environment, int glenz,
	struct mh_scene_corner corners[4],
	struct mh_glenz_coordinates coordinates[4], int *near_clipped,
	int *culled, struct mh_error *error_out)
{
	static const unsigned int source_indices[4] = {0, 1, 2, 5};
	enum mh_error_code status;
	float camera[12], normal[3];
	const float *source_normal;
	size_t vertex;
	unsigned int i, j, row, col;
	int skid, shadow, unavailable;

	skid = source->effect == MH_SCENE_EFFECT_SKIDS;
	shadow = source->powervr && source->effect == MH_SCENE_EFFECT_SHADOWS;
	unavailable = source->effect == MH_SCENE_EFFECT_UNAVAILABLE;
	*culled = 0;
	*near_clipped = 0;
	memset(corners, 0, 4 * sizeof(*corners));
	memset(coordinates, 0, 4 * sizeof(*coordinates));

	for (col = 0; col < 4; col++)
		for (row = 0; row < 3; row++)
			camera[col * 3 + row] = row == 2
				? -camera_matrix[col * 4 + row]
				: camera_matrix[col * 4 + row];

	for (i = 0; i < corner_count; i++) {
		vertex = first + source_indices[i];
		status = mh_project_point(projection, camera,
			source->positions + vertex * 3, &corners[i].point,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (corners[i].point.outcode & MH_CLIP_FAR) {
			*culled = 1;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		*near_clipped |= (corners[i].point.outcode & MH_CLIP_NEAR) != 0;

		if (shadow) {
			corners[i].rgb[0] = source->colors[vertex * 3] * 255;
			continue;
		}

		if (skid)
			continue;

		memcpy(corners[i].rgb, source->material_rgb + vertex * 3,
			sizeof(corners[i].rgb));
		memcpy(corners[i].uv,
			source->mesh->atlas_uv == NULL
				? source->mesh->vertices[vertex].uv
				: source->mesh->atlas_uv + vertex * 2,
			sizeof(corners[i].uv));

		if (MH_RENDER_BUILD_POWERVR && source->powervr &&
			!environment && !unavailable) {
			corners[i].uv[0] *= (float)source->image->width /
				source->texture_width;
			corners[i].uv[1] *= (float)source->image->height /
				source->texture_height;
		}

		if (glenz) {
			coordinates[i] = source->glenz_coordinates[vertex];
			memcpy(corners[i].uv, coordinates[i].reflection,
				sizeof(corners[i].uv));
		} else if (environment) {
			source_normal = source->mesh->vertices[vertex].normal;

			if (MH_RENDER_BUILD_POWERVR && source->powervr)
				status = mh_powervr_environment_model_uv(
					projection, &corners[i].point, camera,
					source_normal, corners[i].uv,
					error_out);
			else {
				for (j = 0; j < 3; j++)
					normal[j] =
						camera[j] * source_normal[0] +
						camera[3 + j] *
							source_normal[1] +
						camera[6 + j] *
							source_normal[2];

				status = mh_glenz_phong_uv(normal,
					corners[i].point.value,
					projection->clip.center,
					projection->inverse_half_width,
					corners[i].uv, error_out);
			}

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
store_vertex(struct mh_vertex *vertex, const struct mh_scene_vertex *corner,
	const struct mh_projection *projection)
{
	unsigned int i;

	memset(vertex, 0, sizeof(*vertex));
	vertex->position[0] =
		2 * corner->xy[0] / projection->clip.extent[0] - 1;
	vertex->position[1] =
		1 - 2 * corner->xy[1] / projection->clip.extent[1];
	vertex->position[2] = 1 - corner->depth;
	vertex->projection_w = 1 / corner->reciprocal_w;
	memcpy(vertex->uv, corner->uv, sizeof(vertex->uv));

	for (i = 0; i < 3; i++)
		vertex->color[i] = corner->rgb[i] / COLOR_NORMALIZER;

	/* Prepared vertices carry fog bytes in the unused normal attributes. */
	vertex->normal[0] = corner->visibility / COLOR_NORMALIZER;
	vertex->normal[1] = corner->background / COLOR_NORMALIZER;
}

static enum mh_error_code
select_material(const struct mh_material_face_source *source, size_t draw_index,
	size_t count, unsigned int type, int reflections,
	struct face_material *material, struct mh_error *error_out)
{
	const struct mh_draw *draw;

	draw = &source->mesh->draws[draw_index];
	material->group = source->mesh->draw_groups == NULL
		? draw->texture != 0
		: source->mesh->draw_groups[draw_index];

	if (material->group > MH_TEXTURE_GROUPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid prepared texture group");

	material->unavailable = source->effect == MH_SCENE_EFFECT_UNAVAILABLE;
	material->skid = source->effect == MH_SCENE_EFFECT_SKIDS;
	material->shadow =
		source->powervr && source->effect == MH_SCENE_EFFECT_SHADOWS;

	if (MH_RENDER_BUILD_POWERVR && source->powervr && source->glenz)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR special-material preparation is not connected yet");

	if ((material->skid || material->shadow) && count != 6)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ground effect requires a quad");

	material->environment = !material->skid && !material->shadow &&
		!material->unavailable && draw->environment && reflections &&
		source->mesh->environment_texture != 0;
	material->glenz = material->environment && source->glenz;

	if ((!material->skid && !material->shadow &&
		source->material_rgb == NULL) ||
		(material->shadow && source->colors == NULL) ||
		(material->glenz && source->glenz_coordinates == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing material face attributes");

	if (MH_RENDER_BUILD_POWERVR && source->powervr && !material->skid &&
		!material->shadow && !material->environment &&
		!material->unavailable &&
		(source->image == NULL || source->texture_width == 0 ||
			source->texture_height == 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing PowerVR material texture dimensions");

	material->pass = material->skid ? MH_SCENE_SOLID
		: material->unavailable ? MH_SCENE_UNAVAILABLE
		: draw->alpha_test	? MH_SCENE_ALPHA
		: material->environment ? MH_SCENE_ENVIRONMENT
		: material->group	? MH_SCENE_TEXTURED
					: MH_SCENE_SOLID;
	material->smooth = type >= 4 || material->environment;
	material->corners = count == 6 ? 4 : 3;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static size_t
material_list(
	const struct mh_scene_face *face, const struct face_material *material)
{
	return material->shadow			       ? MH_LIST_SHADOW
		: material->skid		       ? MH_LIST_SKID
		: material->unavailable		       ? MH_LIST_UNAVAILABLE
		: face->pass == MH_SCENE_DISTANT_SOLID ? MH_LIST_DISTANT_SOLID
		: face->pass == MH_SCENE_DISTANT_ALPHA ? MH_LIST_DISTANT_ALPHA
		: material->pass == MH_SCENE_ALPHA
		? MH_LIST_ALPHA + face->bucket
		: material->environment ? MH_LIST_ENVIRONMENT
		: material->group	? MH_LIST_TEXTURE + material->group - 1
					: MH_LIST_SOLID;
}

static enum mh_error_code
prepare_geometry(const struct mh_material_face_source *source,
	const struct mh_projection *projection,
	const struct face_material *material,
	const struct mh_scene_corner corners[4],
	struct mh_material_faces *result, struct mh_error *error_out)
{
	if (MH_RENDER_BUILD_POWERVR && material->shadow)
		return mh_powervr_shadow_prepare(projection, corners,
			result->faces, &result->count, error_out);

	if (material->skid)
		return mh_skid_face_prepare(projection, corners, result->faces,
			&result->count, error_out);

	if (MH_RENDER_BUILD_POWERVR && source->powervr)
		return mh_powervr_face_prepare(projection, material->pass,
			material->corners, material->smooth, corners,
			result->faces, &result->count, error_out);

	return mh_vertex_color_face_prepare(projection, material->pass,
		material->corners, material->smooth, source->fog_enabled,
		corners, result->faces, &result->count, error_out);
}

enum mh_error_code
mh_material_face_prepare(const struct mh_material_face_source *source,
	size_t draw_index, size_t first, size_t count, unsigned int type,
	const float camera_matrix[16], const struct mh_projection *projection,
	int reflections, struct mh_material_faces *output,
	struct mh_error *error_out)
{
	struct mh_scene_corner corners[4];
	struct mh_material_faces result;
	struct face_material material;
	enum mh_error_code status;
	unsigned int i;
	int culled;

	if (source == NULL || source->mesh == NULL ||
		source->positions == NULL || source->mesh->vertices == NULL ||
		source->mesh->draws == NULL || camera_matrix == NULL ||
		projection == NULL || output == NULL ||
		draw_index >= source->mesh->draw_count ||
		(count != 3 && count != 6) ||
		first > source->mesh->vertex_count ||
		count > source->mesh->vertex_count - first)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material face source");

	memset(&material, 0, sizeof(material));
	status = select_material(source, draw_index, count, type, reflections,
		&material, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.glenz = material.glenz;
	status = project_corners(source, first, material.corners, camera_matrix,
		projection, material.environment, result.glenz, corners,
		result.coordinates, &result.near_clipped, &culled, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (culled) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = prepare_geometry(
		source, projection, &material, corners, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < result.count; i++) {
		if (MH_RENDER_BUILD_POWERVR && source->powervr &&
			!material.shadow)
			mh_powervr_face_color_prepare(
				&result.faces[i], material.smooth);

		result.lists[i] = material_list(&result.faces[i], &material);
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_material_face_vertices(const struct mh_material_face_source *source,
	const struct mh_material_faces *prepared, unsigned int index,
	const struct mh_projection *projection, struct mh_vertex vertices[6],
	struct mh_glenz_coordinates coordinates[6])
{
	static const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
	const struct mh_scene_face *face;
	unsigned int expanded, i, corner;

	face = &prepared->faces[index];
	expanded = face->count == 4 ? 6 : 3;

	for (i = 0; i < expanded; i++) {
		corner = indices[i];
		store_vertex(&vertices[i], &face->corners[corner], projection);

		if (MH_RENDER_BUILD_POWERVR && source->powervr)
			mh_powervr_vertex_color_prepare(&vertices[i], face,
				corner, source->effect, prepared->lists[index]);

		memset(&coordinates[i], 0, sizeof(coordinates[i]));

		if (prepared->glenz) {
			memcpy(coordinates[i].reflection,
				face->corners[corner].uv, 2 * sizeof(float));

			/*
			 * The original skips the Phong pass after near
			 * splitting.
			 */
			if (!prepared->near_clipped)
				memcpy(coordinates[i].phong,
					prepared->coordinates[corner].phong,
					2 * sizeof(float));
		}
	}

	return expanded;
}

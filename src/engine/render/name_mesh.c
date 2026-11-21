#include <string.h>

#include "engine/core/error.h"
#include "engine/render/name_mesh.h"

enum {
	VIEWPORT_LIMIT = 16384,
	COLOR_BYTE_MAX = 255
};

enum mh_error_code
mh_name_mesh_update(struct mh_name_mesh *names, unsigned int eligible_mask,
	const float *positions, const float *radii, const float view[12],
	const struct mh_projection *projection,
	const struct mh_frustum *frustum, struct mh_error *error_out)
{
	static const unsigned int quad_corners[MH_NAME_QUAD_VERTICES] = {
		0, 1, 2, 0, 2, 3};

	struct mh_vertex vertices[MH_NAME_QUAD_VERTICES * MH_NAME_LABELS_MAX],
		*vertex;
	struct mh_name_quad label_quad;
	const struct mh_effect_quad *quad;
	const float *position;
	enum mh_error_code status;
	float camera_position[3], native_depth, clip_w, *rgb;
	float colors[MH_NAME_QUAD_VERTICES * MH_NAME_LABELS_MAX * 3];
	unsigned int entry, index, axis, channel, corner, first_vertex,
		visible_mask;
	int powervr;

	if (names == NULL || positions == NULL || radii == NULL ||
		view == NULL || projection == NULL || frustum == NULL ||
		names->count < 1 || names->count > MH_NAME_LABELS_MAX ||
		names->mesh.vertices == NULL ||
		names->mesh.vertex_count !=
			MH_NAME_QUAD_VERTICES * MH_NAME_LABELS_MAX ||
		!(projection->clip.extent[0] >= 1 &&
			projection->clip.extent[0] <= VIEWPORT_LIMIT) ||
		!(projection->clip.extent[1] >= 1 &&
			projection->clip.extent[1] <= VIEWPORT_LIMIT) ||
		!(projection->clip.near_distance > 0 &&
			projection->far_distance >
				projection->clip.near_distance))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid name mesh frame");

	powervr = names->profile == MH_RENDER_POWERVR;

	if (powervr &&
		(names->mesh.material_rgb == NULL ||
			names->mesh.material_rgb_count !=
				names->mesh.vertex_count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing PowerVR name colors");

	memset(vertices, 0, sizeof(vertices));
	memset(colors, 0, sizeof(colors));
	visible_mask = 0;
	eligible_mask &= names->active_mask;

	/* The original prepends each name to the shared effects list. */
	for (entry = names->count; entry != 0;) {

		entry--;

		if (!(eligible_mask & (1U << entry)))
			continue;

		position = positions + entry * 3;

		if (!mh_frustum_sphere(frustum, position, radii[entry]))
			continue;

		for (axis = 0; axis < 3; axis++)
			camera_position[axis] =
				(float)(((long double)position[1] *
							view[axis + 3] +
						(long double)position[0] *
							view[axis]) +
					(long double)position[2] *
						view[axis + 6] +
					view[axis + 9]);

		status = (powervr ? mh_patched_name_label_project
				: mh_name_label_project)(
			&names->labels[entry], position, camera_position, view,
			projection, &label_quad, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!label_quad.visible)
			continue;

		quad = &label_quad.quad;
		native_depth = (1 - quad->depth) /
			(1 -
				projection->clip.near_distance /
					projection->far_distance);
		clip_w = 1 / label_quad.reciprocal_depth;

		/*
		 * SGL uses reciprocal W; fixed Z only keeps native clipping
		 * valid.
		 */
		if (powervr)
			native_depth = .5f;

		first_vertex =
			(names->count - 1 - entry) * MH_NAME_QUAD_VERTICES;

		for (index = 0; index < MH_NAME_QUAD_VERTICES; index++) {
			corner = quad_corners[index];
			vertex = &vertices[first_vertex + index];
			rgb = colors + (first_vertex + index) * 3;
			vertex->position[0] = quad->xy[corner][0] * 2 /
					projection->clip.extent[0] -
				1;
			vertex->position[1] = 1 -
				quad->xy[corner][1] * 2 /
					projection->clip.extent[1];
			vertex->position[2] = native_depth;
			vertex->projection_w = clip_w;

			for (axis = 0; axis < 2; axis++)
				vertex->uv[axis] = quad->uv[corner][axis];

			for (channel = 0; channel < 3; channel++) {
				rgb[channel] = quad->color[channel];
				vertex->color[channel] =
					(float)(unsigned int)
						quad->color[channel] /
					COLOR_BYTE_MAX;
			}
		}

		visible_mask |= 1U << entry;
	}

	memcpy(names->mesh.vertices, vertices, sizeof(vertices));

	if (powervr)
		memcpy(names->mesh.material_rgb, colors, sizeof(colors));

	names->visible_mask = visible_mask;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/atmosphere_internal.h"
#include "engine/render/atmosphere_lamps.h"
#include "engine/render/powervr_halo.h"

static void
transform_direction(struct mh_attached_halo *style, const float *model_to_world,
	const float world_to_view[16])
{
	double world_direction[3], component;
	unsigned int j, k;

	for (j = 0; j < 3; j++) {
		world_direction[j] =
			model_to_world == NULL ? style->direction[j] : 0;

		if (model_to_world != NULL)
			for (k = 0; k < 3; k++)
				world_direction[j] +=
					(double)model_to_world[k * 4 + j] *
					style->direction[k];
	}

	for (j = 0; j < 3; j++) {
		component = 0;

		for (k = 0; k < 3; k++)
			component += (double)world_to_view[k * 4 + j] *
				world_direction[k];

		style->direction[j] = (float)(j == 2 ? -component : component);
	}
}

enum mh_error_code
mh_lamps_attached_update(struct mh_lamps *lamps, size_t index,
	const struct mh_geometry_halo *halo,
	const struct mh_attached_halo *style, const float *model_to_world,
	const float world_to_clip[16], const float world_to_view[16],
	unsigned int width, unsigned int height, float near_depth,
	float far_depth, float cue_start, struct mh_error *error_out)
{
	struct mh_halo_projection projection;
	struct mh_attached_halo camera_style;
	struct mh_effect_quad quads[ATTACHED_LAMP_QUADS];
	struct mh_depth_probe probe;
	struct mh_vertex staged_vertices[LAMP_VERTICES];
	float staged_colors[LAMP_VERTICES * 3];
	double world_position[3], clip[4], depth, horizontal_focal,
		vertical_focal;
	unsigned int i, j, k, quad_index;
	int in_view, powervr;
	enum mh_error_code status;

	if (lamps == NULL || index < lamps->world_count ||
		index >= lamps->count || halo == NULL || style == NULL ||
		(lamps->halos != 0 && lamps->halos != 1) ||
		(lamps->flares != 0 && lamps->flares != 1) ||
		!mh_atmosphere_camera_valid(world_to_clip, width, height) ||
		!mh_atmosphere_camera_valid(world_to_view, width, height) ||
		(model_to_world != NULL &&
			!mh_atmosphere_camera_valid(
				model_to_world, width, height)) ||
		!(near_depth > 0 && near_depth < far_depth &&
			far_depth <= FAR_DEPTH_LIMIT) ||
		!(cue_start >= 0 && cue_start < 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attached lamp projection");

	powervr = lamps->profile == MH_RENDER_POWERVR;

	if (MH_RENDER_BUILD_POWERVR && powervr &&
		(lamps->mesh.material_rgb == NULL ||
			lamps->mesh.material_rgb_count !=
				lamps->mesh.vertex_count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lamp colors");

	horizontal_focal = sqrt((double)world_to_clip[0] * world_to_clip[0] +
		(double)world_to_clip[4] * world_to_clip[4] +
		(double)world_to_clip[8] * world_to_clip[8]);
	vertical_focal = sqrt((double)world_to_clip[1] * world_to_clip[1] +
		(double)world_to_clip[5] * world_to_clip[5] +
		(double)world_to_clip[9] * world_to_clip[9]);

	if (!(horizontal_focal >= ATTACHED_MIN_FOCAL &&
		horizontal_focal <= FOCAL_LIMIT && vertical_focal > 0 &&
		vertical_focal <= FOCAL_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attached focal scale");

	projection.width = (float)width;
	projection.height = (float)height;
	projection.pixel_aspect =
		(float)(vertical_focal / horizontal_focal * height / width);
	projection.near_depth = near_depth;
	projection.far_depth = far_depth;
	projection.cue_start = cue_start;
	memcpy(projection.color, halo->color, 3);
	camera_style = *style;
	camera_style.inverse_focal = (float)(1 / horizontal_focal);

	transform_direction(&camera_style, model_to_world, world_to_view);

	in_view = 1;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++) {
			if (!(halo->endpoints[i][j] >= -ENDPOINT_LIMIT &&
				halo->endpoints[i][j] <= ENDPOINT_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid attached endpoint");

			world_position[j] = model_to_world == NULL
				? halo->endpoints[i][j]
				: model_to_world[12 + j];

			if (model_to_world != NULL)
				for (k = 0; k < 3; k++)
					world_position[j] +=
						(double)model_to_world[k * 4 +
							j] *
						halo->endpoints[i][k];
		}

		for (j = 0; j < 4; j++) {
			clip[j] = world_to_clip[12 + j];

			for (k = 0; k < 3; k++)
				clip[j] += (double)world_to_clip[k * 4 + j] *
					world_position[k];
		}

		if (clip[3] <= near_depth || clip[3] >= far_depth) {
			in_view = 0;
			continue;
		}

		projection.endpoint[i][0] =
			(float)((clip[0] / clip[3] + 1) * width * .5);
		projection.endpoint[i][1] =
			(float)((1 - clip[1] / clip[3]) * height * .5);
		projection.endpoint[i][2] = (float)(clip[3] / far_depth);
		projection.endpoint[i][3] = (float)(1 / clip[3]);
	}

	memset(staged_vertices, 0, sizeof(staged_vertices));
	memset(staged_colors, 0, sizeof(staged_colors));
	memset(&probe, 0, sizeof(probe));

	if (in_view && lamps->halos) {
		status = MH_RENDER_BUILD_POWERVR && powervr
			? mh_powervr_attached_halo_quads(&projection,
				&camera_style, lamps->flares, quads,
				error_out)
			: mh_attached_halo_quads(&projection, &camera_style,
				lamps->visible[index] != 0, lamps->flares,
				&probe, quads, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (probe.active) {
			depth = (1 - (double)probe.depth) /
				(1 - (double)near_depth / far_depth);
			probe.depth = (float)(depth < 0 ? 0
					: depth > 1	? 1
							: depth);
		}

		/* Prepending produces seven hex quads, then the single glow. */
		for (i = 0; i < ATTACHED_LAMP_QUADS; i++) {
			quad_index = ATTACHED_LAMP_QUADS - 1 - i;

			if (MH_RENDER_BUILD_POWERVR && powervr) {
				mh_atmosphere_powervr_quad(
					staged_vertices + i * QUAD_VERTICES,
					staged_colors + i * QUAD_VERTICES * 3,
					&quads[quad_index], width, height);
				continue;
			}

			depth = (1 - (double)quads[quad_index].depth) /
				(1 - (double)near_depth / far_depth);

			if (depth < 0)
				depth = 0;

			if (depth > 1)
				depth = 1;

			if (quad_index != 0)
				for (j = 0; j < 4; j++)
					quads[quad_index].uv[j][0] -= .5f;

			mh_atmosphere_quad_vertices(
				staged_vertices + i * QUAD_VERTICES,
				&quads[quad_index], width, height, (float)depth,
				quad_index == 0 ? 2 : 4);
		}
	}

	memcpy(lamps->mesh.vertices + index * LAMP_VERTICES, staged_vertices,
		sizeof(staged_vertices));

	if (MH_RENDER_BUILD_POWERVR && powervr)
		memcpy(lamps->mesh.material_rgb + index * LAMP_VERTICES * 3,
			staged_colors, sizeof(staged_colors));

	lamps->probes[index] = probe;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

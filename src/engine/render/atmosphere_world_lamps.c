#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/atmosphere_internal.h"
#include "engine/render/atmosphere_lamps.h"
#include "engine/render/powervr_halo.h"

static enum mh_error_code
update_world_halos(struct mh_mesh *mesh, const struct mh_world *world,
	const float world_to_clip[16], unsigned int width, unsigned int height,
	float near_depth, float far_depth, float cue_start,
	struct mh_lamps *lamps, struct mh_error *error_out)
{
	struct mh_geometry_halo halo;
	struct mh_depth_probe *probe;
	struct mh_halo_projection projection;
	struct mh_effect_quad quads[WORLD_LAMP_QUADS];
	double clip[4], horizontal_focal, vertical_focal, depth;
	size_t source_index;
	unsigned int index, row, axis, quads_per_source, quad_index;
	size_t world_count, capacity, first_vertex;
	int visible, powervr;
	enum mh_error_code status;

	if (mesh == NULL || world == NULL ||
		!mh_atmosphere_camera_valid(world_to_clip, width, height) ||
		!(near_depth > 0 && near_depth < far_depth &&
			far_depth <= FAR_DEPTH_LIMIT) ||
		!(cue_start >= 0 && cue_start < 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid halo mesh camera");

	powervr = lamps != NULL && lamps->profile == MH_RENDER_POWERVR;

	if (MH_RENDER_BUILD_POWERVR && powervr &&
		(mesh->material_rgb == NULL ||
			mesh->material_rgb_count != mesh->vertex_count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lamp colors");

	world_count = world->halo_count;
	quads_per_source = lamps == NULL ? GLOW_QUADS : WORLD_LAMP_QUADS;
	capacity = lamps == NULL ? world_count : lamps->count;

	if (capacity == 0)
		capacity = 1;

	if (world_count > MH_LAMP_LIMIT || capacity > MH_LAMP_LIMIT ||
		mesh->vertex_count !=
			capacity * quads_per_source * QUAD_VERTICES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid halo mesh size");

	horizontal_focal = sqrt((double)world_to_clip[0] * world_to_clip[0] +
		(double)world_to_clip[4] * world_to_clip[4] +
		(double)world_to_clip[8] * world_to_clip[8]);
	vertical_focal = sqrt((double)world_to_clip[1] * world_to_clip[1] +
		(double)world_to_clip[5] * world_to_clip[5] +
		(double)world_to_clip[9] * world_to_clip[9]);

	if (!(horizontal_focal > 0 && horizontal_focal <= FOCAL_LIMIT &&
		vertical_focal > 0 && vertical_focal <= FOCAL_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid halo focal scale");

	projection.width = (float)width;
	projection.height = (float)height;
	projection.pixel_aspect =
		(float)(vertical_focal / horizontal_focal * height / width);
	projection.near_depth = near_depth;
	projection.far_depth = far_depth;
	projection.cue_start = cue_start;
	memset(mesh->vertices, 0, mesh->vertex_count * sizeof(*mesh->vertices));

	if (MH_RENDER_BUILD_POWERVR && powervr)
		memset(mesh->material_rgb, 0,
			mesh->vertex_count * 3 * sizeof(float));

	if (lamps != NULL)
		memset(lamps->probes, 0,
			(lamps->count == 0 ? 1 : lamps->count) *
				sizeof(*lamps->probes));

	if (lamps != NULL && !lamps->halos)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (source_index = 0; source_index < world_count; source_index++) {
		if (lamps != NULL && lamps->world_hidden != NULL &&
			lamps->world_hidden[source_index])
			continue;

		halo = world->halos[source_index];

		visible = 1;

		for (index = 0; index < 2; index++) {
			for (row = 0; row < 4; row++) {
				clip[row] = world_to_clip[12 + row];

				for (axis = 0; axis < 3; axis++)
					clip[row] +=
						(double)world_to_clip[axis * 4 +
							row] *
						halo.endpoints[index][axis];
			}

			if (clip[3] <= near_depth || clip[3] >= far_depth) {
				visible = 0;
				break;
			}

			projection.endpoint[index][0] =
				(float)((clip[0] / clip[3] + 1) * width * .5);
			projection.endpoint[index][1] =
				(float)((1 - clip[1] / clip[3]) * height * .5);
			projection.endpoint[index][2] =
				(float)(clip[3] / far_depth);
			projection.endpoint[index][3] = (float)(1 / clip[3]);
		}

		if (!visible)
			continue;

		probe = lamps == NULL ? NULL : &lamps->probes[source_index];
		memcpy(projection.color, halo.color, 3);

		if (MH_RENDER_BUILD_POWERVR && powervr)
			status = mh_powervr_world_lamp_quads(
				&projection, lamps->flares, quads, error_out);
		else if (lamps == NULL)
			status = mh_world_halo_quads(
				&projection, quads, error_out);
		else
			status = mh_world_lamp_quads(&projection,
				lamps->visible[source_index] != 0,
				lamps->flares, probe, quads, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (probe != NULL && probe->active) {
			depth = (1 - (double)probe->depth) /
				(1 - (double)near_depth / far_depth);
			probe->depth = (float)(depth < 0 ? 0
					: depth > 1	 ? 1
							: depth);
		}

		for (index = 0; index < quads_per_source; index++) {
			quad_index = lamps == NULL
				? index
				: quads_per_source - 1 - index;

			first_vertex =
				(source_index * quads_per_source + index) *
				QUAD_VERTICES;

			if (MH_RENDER_BUILD_POWERVR && powervr) {
				mh_atmosphere_powervr_quad(
					mesh->vertices + first_vertex,
					mesh->material_rgb + first_vertex * 3,
					&quads[quad_index], width, height);
				continue;
			}

			depth = (1 - (double)quads[quad_index].depth) /
				(1 - (double)near_depth / far_depth);

			if (depth < 0)
				depth = 0;

			if (depth > 1)
				depth = 1;

			if (quad_index >= GLOW_QUADS)
				for (row = 0; row < 4; row++)
					quads[quad_index].uv[row][0] -= .5f;

			mh_atmosphere_quad_vertices(
				mesh->vertices + first_vertex,
				&quads[quad_index], width, height, (float)depth,
				quad_index < GLOW_QUADS ? 2 : 4);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_halo_mesh_update(struct mh_mesh *mesh, const struct mh_world *world,
	const float world_to_clip[16], unsigned int width, unsigned int height,
	float near_depth, float far_depth, float cue_start,
	struct mh_error *error_out)
{
	return update_world_halos(mesh, world, world_to_clip, width, height,
		near_depth, far_depth, cue_start, NULL, error_out);
}

enum mh_error_code
mh_lamps_world_cull(struct mh_lamps *lamps, const struct mh_world *world,
	const struct mh_world_view *cell_view, struct mh_error *error_out)
{
	struct mh_world_cell cell;
	unsigned long x, z;

	if (lamps == NULL || world == NULL ||
		lamps->world_count != world->halo_count ||
		(lamps->world_count != 0 && lamps->world_hidden == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world lamp visibility storage");

	if (cell_view != NULL &&
		(cell_view->first[0] > cell_view->end[0] ||
			cell_view->end[0] > world->width ||
			cell_view->first[1] > cell_view->end[1] ||
			cell_view->end[1] > world->height))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lamp cell rectangle");

	if (lamps->world_count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(lamps->world_hidden, cell_view != NULL, lamps->world_count);

	if (cell_view != NULL)
		for (z = cell_view->first[1]; z < cell_view->end[1]; z++)
			for (x = cell_view->first[0]; x < cell_view->end[0];
				x++) {
				/*
				 * The parsed immutable world already bounds
				 * every cell/span.
				 */
				cell = world->cells[(size_t)z * world->width +
					x];

				if (cell.halo_count != 0)
					memset(lamps->world_hidden +
							cell.halo_first,
						0, (size_t)cell.halo_count);
			}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lamps_update(struct mh_lamps *lamps, const struct mh_world *world,
	const float world_to_clip[16], unsigned int width, unsigned int height,
	float near_depth, float far_depth, float cue_start,
	struct mh_error *error_out)
{
	if (lamps == NULL || world == NULL ||
		lamps->world_count != world->halo_count ||
		lamps->count < lamps->world_count ||
		(lamps->halos != 0 && lamps->halos != 1) ||
		(lamps->flares != 0 && lamps->flares != 1) ||
		lamps->probes == NULL || lamps->visible == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lamp state");

	return update_world_halos(&lamps->mesh, world, world_to_clip, width,
		height, near_depth, far_depth, cue_start, lamps, error_out);
}

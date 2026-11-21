#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/mesh_internal.h"

/* (Y*Ny + X*Nx) + (Z*Nz + D), strictly positive. */
static int
software_face_visible(const float plane[4], const float camera[3])
{
	float xy, zd;

	xy = (float)((float)(plane[1] * camera[1]) +
		(float)(plane[0] * camera[0]));
	zd = (float)((float)(plane[2] * camera[2]) + plane[3]);

	return (float)(xy + zd) > 0;
}

int
mh_mesh_world_software_ready(const struct mh_mesh *mesh)
{
	return mesh != NULL && mesh->world != NULL && mesh->world->native &&
		mesh->world->ordered &&
		mesh->world->visibility_profile == MH_RENDER_SOFTWARE &&
		mesh->primitive_hidden != NULL &&
		mesh->world->plane_count == mesh->world->span.primitive_count &&
		mesh->world->plane_count <= mesh->primitive_count;
}

int
mh_mesh_world_cell_primitive(
	const struct mh_mesh *mesh, size_t cell, size_t *first)
{
	const struct mh_mesh_world *world;
	size_t model;

	if (mesh == NULL || mesh->world == NULL || first == NULL)
		return 0;

	world = mesh->world;

	if (!world->ordered || cell >= world->cell_count)
		return 0;

	for (; cell < world->cell_count; cell++) {
		model = world->cells[cell].model;

		if (model != MH_WORLD_NO_MODEL) {
			*first = world->models[model].primitive_first;
			return 1;
		}
	}

	*first = world->span.primitive_count;

	return 1;
}

int
mh_mesh_scenery_bounds(const struct mh_mesh_scenery *scenery,
	const struct mh_mesh_binding *binding, float position[3], float *radius)
{
	float center[3], extent;
	double value;
	unsigned int j;

	if (!(scenery->fit_radius > 0))
		return 0;

	value = binding->radius / scenery->fit_radius;

	if (!(value >= 0 && value <= FLT_MAX))
		return 0;

	extent = (float)value;

	for (j = 0; j < 3; j++) {
		value = (binding->pose.position[j] - scenery->fit_center[j]) /
			scenery->fit_radius;

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return 0;

		center[j] = (float)value;
	}

	memcpy(position, center, sizeof(center));
	*radius = extent;

	return 1;
}

enum mh_error_code
mh_mesh_scene_cull_profile(enum mh_render_profile profile, struct mh_mesh *mesh,
	const struct mh_frustum *frustum, struct mh_error *error_out)
{
	struct mh_mesh_binding *binding;
	float position[3], radius;

	if (mesh == NULL || (unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery visibility input");

	if (mesh->scenery == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mesh->draw_hidden == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scenery visibility storage");

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		if (binding->draw_first > mesh->draw_count ||
			binding->draw_count >
				mesh->draw_count - binding->draw_first)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"scenery draw range exceeds mesh");

		if (!mh_mesh_scenery_bounds(
			mesh->scenery, binding, position, &radius))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery bounds");

		binding->hidden = frustum != NULL &&
			!mh_frustum_sphere_profile(
				profile, frustum, position, radius);
	}

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next)
		memset(mesh->draw_hidden + binding->draw_first, binding->hidden,
			binding->draw_count);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_world_cull(struct mh_mesh *mesh, const struct mh_world_view *view,
	struct mh_error *error_out)
{
	struct mh_mesh_world *world;
	struct mh_mesh_world_model *model;
	const struct mh_world_cell *cell;
	size_t i;
	unsigned long x, z;

	if (mesh == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing world mesh");

	world = mesh->world;

	if (world == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mesh->draw_hidden == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing world visibility storage");

	if (view != NULL &&
		(unsigned int)view->profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world visibility profile");

	if (view != NULL && view->profile == MH_RENDER_SOFTWARE) {
		if (world->planes == NULL || mesh->primitive_hidden == NULL ||
			world->plane_count > mesh->primitive_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing world face visibility storage");

		for (i = 0; i < 3; i++)
			if (!(view->camera[i] >= -MH_MESH_POSITION_LIMIT &&
				view->camera[i] <= MH_MESH_POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid world visibility camera");
	}

	if (view != NULL &&
		(view->first[0] > view->end[0] || view->end[0] > world->width ||
			view->first[1] > view->end[1] ||
			view->end[1] > world->height))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid visible cell rectangle");

	for (i = 0; i < world->model_count; i++) {
		model = &world->models[i];

		if (model->draw_first > mesh->draw_count ||
			model->draw_count >
				mesh->draw_count - model->draw_first)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"world draw range exceeds mesh");

		model->hidden = view != NULL;
	}

	if (view != NULL)
		for (z = view->first[1]; z < view->end[1]; z++)
			for (x = view->first[0]; x < view->end[0]; x++) {
				cell = &world->cells[(size_t)z * world->width +
					x];

				if (cell->model == MH_WORLD_NO_MODEL)
					continue;

				model = &world->models[cell->model];

				if (mh_frustum_sphere_profile(view->profile,
					&view->frustum, model->center,
					model->radius))
					model->hidden = 0;
			}

	for (i = 0; i < world->model_count; i++) {
		model = &world->models[i];
		memset(mesh->draw_hidden + model->draw_first, model->hidden,
			model->draw_count);
	}

	if (mesh->primitive_hidden != NULL) {
		memset(mesh->primitive_hidden, 0, mesh->primitive_count);

		if (view != NULL && view->profile == MH_RENDER_SOFTWARE)
			for (i = 0; i < world->plane_count; i++)
				mesh->primitive_hidden[i] =
					(unsigned char)!software_face_visible(
						world->planes[i], view->camera);
	}

	world->visibility_profile =
		view == NULL ? MH_RENDER_GLIDE : view->profile;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_scene_cull(struct mh_mesh *mesh, const struct mh_frustum *frustum,
	struct mh_error *error_out)
{
	return mh_mesh_scene_cull_profile(
		MH_RENDER_GLIDE, mesh, frustum, error_out);
}

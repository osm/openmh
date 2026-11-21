#include <string.h>

#include "engine/render/mesh_internal.h"

size_t
mh_mesh_scene_halo_count(const struct mh_mesh *mesh)
{
	const struct mh_mesh_binding *binding;
	size_t count;

	if (mesh == NULL || mesh->scenery == NULL)
		return 0;

	count = 0;

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next)
		count += binding->halo_count;

	return count;
}

static const struct mh_mesh_binding *
scene_halo_binding(const struct mh_mesh *mesh, size_t *index)
{
	const struct mh_mesh_binding *binding;

	if (mesh == NULL || mesh->scenery == NULL)
		return NULL;

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		if (*index < binding->halo_count)
			break;

		*index -= binding->halo_count;
	}

	return binding;
}

int
mh_mesh_scene_halo_span(const struct mh_mesh *mesh, size_t scene_index,
	size_t *first, size_t *count)
{
	const struct mh_mesh_binding *binding;
	size_t offset;

	if (mesh == NULL || mesh->scenery == NULL || first == NULL ||
		count == NULL)
		return 0;

	offset = 0;

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		if (binding->scene_index == scene_index) {
			*first = offset;
			*count = binding->halo_count;
			return 1;
		}

		offset += binding->halo_count;
	}

	return 0;
}

int
mh_mesh_scene_halo_bounds(const struct mh_mesh *mesh, size_t index,
	float position[3], float *radius)
{
	const struct mh_mesh_binding *binding;

	if (position == NULL || radius == NULL)
		return 0;

	binding = scene_halo_binding(mesh, &index);

	return binding != NULL &&
		mh_mesh_scenery_bounds(
			mesh->scenery, binding, position, radius);
}

int
mh_mesh_scene_halo(const struct mh_mesh *mesh, size_t index,
	struct mh_geometry_halo *halo, float tint[3])
{
	const struct mh_mesh_binding *binding;
	const struct mh_geometry_halo *local;
	struct mh_geometry_halo result;
	float rgb[3];
	double value;
	unsigned int endpoint, axis, component;

	if (halo == NULL || tint == NULL)
		return 0;

	binding = scene_halo_binding(mesh, &index);

	if (binding == NULL)
		return 0;

	local = binding->halos + index;
	result = *local;

	for (axis = 0; axis < 3; axis++) {
		value = binding->pose.color[axis];

		if (!(value >= -MH_MESH_TINT_LIMIT &&
			value <= MH_MESH_TINT_LIMIT))
			return 0;

		rgb[axis] = (float)value;

		for (endpoint = 0; endpoint < 2; endpoint++) {
			value = binding->pose.position[axis];

			for (component = 0; component < 3; component++)
				value += binding->pose.rotation[component * 3 +
						axis] *
					local->endpoints[endpoint][component];

			value = (value - mesh->scenery->fit_center[axis]) /
				mesh->scenery->fit_radius;

			if (!(value >= -MH_MESH_POSITION_LIMIT &&
				value <= MH_MESH_POSITION_LIMIT))
				return 0;

			result.endpoints[endpoint][axis] = value;
		}
	}

	*halo = result;
	memcpy(tint, rgb, sizeof(rgb));

	return 1;
}

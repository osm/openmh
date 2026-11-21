#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/render/mesh_internal.h"

#define TIME_LIMIT 1e9

enum mh_error_code
mh_mesh_transform_vertices(const struct mh_vertex *local,
	struct mh_vertex *output, size_t count,
	const struct mh_scene_object *pose, const struct mh_mesh_scenery *fit,
	struct mh_error *error_out)
{
	size_t i;
	unsigned int axis;
	unsigned int k;
	double position;
	double normal;
	double color;

	for (i = 0; i < count; i++) {
		output[i] = local[i];

		for (axis = 0; axis < 3; axis++) {
			position = pose->position[axis];
			normal = 0.0;

			for (k = 0; k < 3; k++) {
				position += pose->rotation[k * 3 + axis] *
					local[i].position[k];
				normal += pose->rotation[k * 3 + axis] *
					local[i].normal[k];
			}

			if (!(position >= -FLT_MAX && position <= FLT_MAX) ||
				!(normal >= -FLT_MAX && normal <= FLT_MAX)) {
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"scenery transform exceeds mesh float range");
			}

			output[i].position[axis] = (float)position;

			if (fit != NULL) {
				position = (output[i].position[axis] -
						fit->fit_center[axis]) /
					fit->fit_radius;

				if (!(position >= -FLT_MAX &&
					position <= FLT_MAX)) {
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"animated scenery exceeds fitted float range");
				}

				output[i].position[axis] = (float)position;
			}

			output[i].normal[axis] = (float)normal;
			color = local[i].color[axis] * pose->color[axis];

			/*
			 * Inspection output range; the raw motion curve is not
			 * clamped.
			 */
			if (color < 0)
				color = 0;

			if (color > 1)
				color = 1;

			output[i].color[axis] = (float)color;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_animate_objects(struct mh_mesh *mesh, double seconds,
	const struct mh_scene_transform *poses, size_t count,
	struct mh_error *error_out)
{
	struct mh_mesh_binding *binding;
	struct mh_scene_object pose;
	size_t i, j;
	enum mh_error_code status;

	if (count != 0 && poses == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing scenery poses");

	if (mesh == NULL || !(seconds >= 0 && seconds <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mesh animation time");

	if (mesh->scenery == NULL)
		return count == 0
			? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: mh_error_set(error_out, MH_ERROR_FAILED,
				"scenery mesh has no pose bindings");

	for (i = 0; i < count; i++) {
		for (j = 0; j < i; j++)
			if (poses[j].scene_index == poses[i].scene_index)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate scenery pose");

		for (j = 0; j < 3; j++)
			if (!(poses[i].position[j] >= -MH_MESH_POSITION_LIMIT &&
				poses[i].position[j] <=
					MH_MESH_POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scenery position");

		/*
		 * Physical scenery retains the original unnormalized contact
		 * basis.
		 */
		if (!mh_rotation_valid_accumulated(poses[i].rotation))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery rotation");

		for (binding = mesh->scenery->bindings; binding != NULL;
			binding = binding->next)
			if (binding->scene_index == poses[i].scene_index)
				break;

		if (binding == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unknown scenery pose");
	}

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		pose = binding->source;
		status = pose.key_count != 0
			? mh_scene_sample(
				&binding->source, seconds, &pose, error_out)
			: mh_error_set(error_out, MH_ERROR_NONE, NULL);

		for (i = 0; i < count; i++)
			if (binding->scene_index == poses[i].scene_index) {
				memcpy(pose.position, poses[i].position,
					sizeof(pose.position));
				memcpy(pose.rotation, poses[i].rotation,
					sizeof(pose.rotation));
				break;
			}

		binding->pending_pose = pose;

		if (status == MH_ERROR_NONE)
			status = mh_mesh_transform_vertices(
				binding->local_vertices,
				binding->staged_vertices, binding->vertex_count,
				&pose, mesh->scenery, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		binding->pose = binding->pending_pose;
		memcpy(mesh->vertices + binding->vertex_first,
			binding->staged_vertices,
			binding->vertex_count *
				sizeof(*binding->staged_vertices));
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_animate(
	struct mh_mesh *mesh, double seconds, struct mh_error *error_out)
{
	return mh_mesh_animate_objects(mesh, seconds, NULL, 0, error_out);
}

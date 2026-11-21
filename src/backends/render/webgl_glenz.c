#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_mesh.h"

#define COORDINATE_LIMIT 1000000.0f

static enum mh_error_code
validate(const struct mh_webgl_mesh *mesh,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	size_t i;
	unsigned int axis;
	float reflection, phong;

	if (mesh->pool->powervr)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR does not support Glenz materials");

	if (glenz->coordinates == NULL ||
		glenz->count != mesh->geometry.count ||
		glenz->reflection_texture >= mesh->source->texture_count ||
		glenz->phong_texture >= mesh->source->texture_count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Glenz material");

	for (i = 0; i < glenz->count; i++)
		for (axis = 0; axis < 2; axis++) {
			reflection = glenz->coordinates[i].reflection[axis];
			phong = glenz->coordinates[i].phong[axis];

			if (!(reflection >= -COORDINATE_LIMIT &&
				reflection <= COORDINATE_LIMIT) ||
				!(phong >= 0 && phong <= 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Glenz coordinates");
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_mesh_glenz(struct mh_webgl_mesh *mesh,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	struct mh_glenz_coordinates *coordinates;
	enum mh_error_code status;
	size_t bytes;

	if (mesh == NULL || mesh->source == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL Glenz mesh");

	if (glenz == NULL) {
		mesh->glenz = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = validate(mesh, glenz, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	bytes = glenz->count * sizeof(*coordinates);
	coordinates = mesh->glenz_coordinates;

	if (coordinates == NULL) {
		coordinates = malloc(bytes);

		if (coordinates == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot retain Glenz coordinates");
	}

	memcpy(coordinates, glenz->coordinates, bytes);
	mesh->glenz_coordinates = coordinates;
	mesh->glenz_reflection = glenz->reflection_texture;
	mesh->glenz_phong = glenz->phong_texture;
	mesh->glenz = 1;
	mesh->glenz_pending = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

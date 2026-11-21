#include <stdlib.h>
#include <string.h>

#include "engine/assets/name_mesh.h"
#include "engine/core/error.h"

void
mh_name_mesh_free(struct mh_name_mesh *names)
{
	if (names == NULL)
		return;

	mh_mesh_free(&names->mesh);
	memset(names, 0, sizeof(*names));
}

enum mh_error_code
mh_name_mesh_allocate(struct mh_name_mesh *result, struct mh_error *error_out)
{
	unsigned int i;

	result->mesh.vertices = (struct mh_vertex *)calloc(
		MH_NAME_QUAD_VERTICES * MH_NAME_LABELS_MAX,
		sizeof(struct mh_vertex));
	result->mesh.draws = (struct mh_draw *)calloc(
		MH_NAME_LABELS_MAX, sizeof(struct mh_draw));
	result->mesh.textures =
		(struct mh_image *)calloc(1, sizeof(struct mh_image));

	if (result->mesh.vertices == NULL || result->mesh.draws == NULL ||
		result->mesh.textures == NULL) {
		mh_name_mesh_free(result);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating name mesh");
	}

	result->mesh.vertex_count = MH_NAME_QUAD_VERTICES * MH_NAME_LABELS_MAX;
	result->mesh.draw_count = MH_NAME_LABELS_MAX;
	result->mesh.texture_count = 1;

	for (i = 0; i < MH_NAME_LABELS_MAX; i++) {
		result->mesh.draws[i].first = i * MH_NAME_QUAD_VERTICES;
		result->mesh.draws[i].count = MH_NAME_QUAD_VERTICES;
		result->mesh.draws[i].prelit = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_name_mesh_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_mesh *output,
	struct mh_error *error_out)
{
	struct mh_name_atlas atlas;
	struct mh_name_mesh result;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing name mesh output");

	status = mh_name_atlas_create(
		font, profiles, count, active_mask, &atlas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.count = count;
	result.active_mask = active_mask;
	memcpy(result.labels, atlas.labels, sizeof(result.labels));
	status = mh_name_mesh_allocate(&result, error_out);

	if (status != MH_ERROR_NONE) {
		mh_name_atlas_free(&atlas);
		return status;
	}

	result.mesh.textures[0] = atlas.image;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

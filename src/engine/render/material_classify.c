#include "engine/render/fog.h"
#include "engine/render/material_classify.h"
#include "engine/render/mesh.h"

static void
primitive_depths(const float *positions, size_t first, unsigned int count,
	const float view[16], const float clip[16], float inverse_far,
	float depths[4], unsigned int *near_mask, int *near_clipped)
{
	static const unsigned int corners[4] = {0, 1, 2, 5};
	const float *position;
	float near_distance;
	unsigned int i;

	*near_mask = 0;

	for (i = 0; i < count; i++) {
		position = positions + (first + corners[i]) * 3;
		depths[i] = -(view[2] * position[0] + view[6] * position[1] +
				view[10] * position[2] + view[14]) *
			inverse_far;
		near_distance = clip[2] * position[0] + clip[6] * position[1] +
			clip[10] * position[2] + clip[14];

		if (near_distance < 0)
			*near_mask |= 1U << i;

		/* Exact near-plane equality also suppresses original Phong. */
		if (near_distance <= 0)
			*near_clipped = 1;
	}
}

enum mh_error_code
mh_material_classify(const struct mh_material_classification *state,
	size_t draw_index, size_t first, size_t count, const float view[16],
	const float clip[16], size_t *list, int *near_clipped,
	struct mh_error *error_out)
{
	const struct mh_draw *draw;
	float depths[4];
	unsigned int corners, near_mask, group;
	int distant, bucket;

	*near_clipped = 0;
	draw = &state->mesh->draws[draw_index];

	if (state->unavailable) {
		*list = MH_LIST_UNAVAILABLE;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	corners = count == 6 ? 4 : 3;
	primitive_depths(state->positions, first, corners, view, clip,
		state->inverse_far, depths, &near_mask, near_clipped);
	distant = mh_fog_distant(depths, corners);

	if (distant < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"nonfinite material depth");

	if (state->fog_enabled && distant)
		*list = draw->alpha_test ? MH_LIST_DISTANT_ALPHA
					: MH_LIST_DISTANT_SOLID;
	else if (draw->alpha_test) {
		bucket = mh_fog_alpha_bucket(depths, corners, near_mask);

		if (bucket < 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid alpha depth bucket");

		*list = MH_LIST_ALPHA + (size_t)bucket;
	} else if (state->glenz && draw->environment && state->reflections &&
		state->mesh->environment_texture != 0)
		*list = MH_LIST_ENVIRONMENT;
	else if (state->mesh->draw_groups == NULL)
		*list = MH_LIST_INSPECTION;
	else {
		group = state->mesh->draw_groups[draw_index];

		if (group > MH_TEXTURE_GROUPS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material texture group");

		if (draw->environment && state->reflections &&
			state->mesh->environment_texture != 0)
			*list = MH_LIST_ENVIRONMENT;
		else
			*list = group == 0 ? MH_LIST_SOLID
					: MH_LIST_TEXTURE + group - 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

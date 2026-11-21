#include "engine/render/submission.h"

enum mh_error_code
mh_scene_submission_check(const struct mh_renderer_scene_part *parts,
	size_t count, size_t mesh_count, struct mh_error *error_out)
{
	const struct mh_renderer_scene_part *part;
	size_t index;

	if ((count != 0 && parts == NULL) ||
		count > (size_t)-1 / sizeof(*parts))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene submission order");

	if (count == 0 && parts != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty scene submission order");

	for (index = 0; index < count; index++) {
		part = &parts[index];

		if ((part->model_role != MH_RENDER_MODEL_ROOT &&
			part->model_role != MH_RENDER_MODEL_CHILD &&
			part->model_role != MH_RENDER_MODEL_WORLD) ||
			(part->model_role == MH_RENDER_MODEL_CHILD &&
				(index == 0 ||
					parts[index - 1].model_role ==
						MH_RENDER_MODEL_WORLD)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene model role");

		if (part->mesh >= mesh_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene submission mesh");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_scene_span_check(const struct mh_mesh *mesh, const struct mh_mesh_span *span,
	struct mh_error *error_out)
{
	size_t index;

	if (mesh == NULL || span == NULL || mesh->lod_count != 0 ||
		span->draw_first > mesh->draw_count ||
		span->draw_count > mesh->draw_count - span->draw_first ||
		span->primitive_first > mesh->primitive_count ||
		span->primitive_count >
			mesh->primitive_count - span->primitive_first)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid fixed scene span");

	if (span->primitive_count != 0 && mesh->primitives == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scene primitives");

	for (index = span->primitive_first;
		index < span->primitive_first + span->primitive_count; index++)
		if (mesh->primitives[index].draw < span->draw_first ||
			mesh->primitives[index].draw - span->draw_first >=
				span->draw_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"scene primitive outside its draw span");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Called only after each range has been checked against its mesh capacity. */
static int
overlap(size_t first, size_t count, size_t other, size_t other_count)
{
	return count != 0 && other_count != 0 && first < other + other_count &&
		other < first + count;
}

enum mh_error_code
mh_scene_submission_mesh_check(const struct mh_mesh *mesh, size_t index,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	const struct mh_mesh_span *span, *other;
	const struct mh_renderer_scene_part *part, *prior;
	enum mh_error_code status;
	size_t part_index, previous, draws, primitives, whole_count, matches;

	if (mesh == NULL || (count != 0 && parts == NULL) ||
		count > (size_t)-1 / sizeof(*parts))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene submission mesh");

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (part_index = 0; part_index < count; part_index++) {
		part = &parts[part_index];

		if (part->mesh != index || part->active_lod)
			continue;

		status = mh_scene_span_check(mesh, &part->span, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	draws = primitives = whole_count = matches = 0;

	for (part_index = 0; part_index < count; part_index++) {
		part = &parts[part_index];

		if (part->mesh != index)
			continue;

		matches++;

		if (part->active_lod)
			whole_count++;
		else {
			span = &part->span;

			if (span->draw_count > mesh->draw_count - draws ||
				span->primitive_count >
					mesh->primitive_count - primitives)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate scene geometry");

			draws += span->draw_count;
			primitives += span->primitive_count;
		}

		for (previous = 0; previous < part_index; previous++) {
			prior = &parts[previous];

			if (prior->mesh != index)
				continue;

			span = &part->span;
			other = &prior->span;

			if (part->active_lod || prior->active_lod ||
				overlap(span->draw_first, span->draw_count,
					other->draw_first, other->draw_count) ||
				overlap(span->primitive_first,
					span->primitive_count,
					other->primitive_first,
					other->primitive_count))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"overlapping scene spans");
		}
	}

	if (matches == 0 ||
		(whole_count != 1 &&
			(draws != mesh->draw_count ||
				primitives != mesh->primitive_count)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete scene submission order");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

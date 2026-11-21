#include <stdlib.h>

#include "engine/core/error.h"
#include "engine/render/scene.h"

enum mh_error_code
mh_race_scene_order_effects(const struct mh_race_scene *scene,
	unsigned int cars, struct mh_scene_effect_slot **output, size_t *count,
	struct mh_error *error_out)
{
	struct mh_scene_effect_slot *order;
	size_t total, scenery_count, spans, index, first, sources, n, slot,
		visited;
	unsigned int entry, mesh;

	if (scene == NULL || output == NULL || count == NULL || cars < 1 ||
		cars > MH_NAME_LABELS_MAX ||
		scene->visuals.lamps.world_count > scene->visuals.lamps.count ||
		scene->visuals.lamps.count > MH_LAMP_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shared effect pool");

	scenery_count = mh_mesh_scene_halo_count(&scene->assets.meshes[0]);

	if (scenery_count >
		scene->visuals.lamps.count - scene->visuals.lamps.world_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery effect pool");

	total = scene->visuals.lamps.count + MH_NAME_LABELS_MAX;

	if (total > (size_t)-1 / sizeof(*order))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many shared effects");

	order = (struct mh_scene_effect_slot *)calloc(total, sizeof(*order));

	if (order == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory ordering shared effects");

	index = total;

	/*
	 * Each descriptor is prepended to the effect list. Build backward from
	 * producer order. Unused names are harmless cleared tail slots.
	 */
	for (entry = cars; entry < MH_NAME_LABELS_MAX; entry++) {
		order[--index].name = 1;
		order[index].source = entry;
	}

	for (n = 0; n < scene->visuals.lamps.world_count; n++)
		order[--index].source = n;

	slot = scene->visuals.lamps.world_count + scenery_count;

	for (entry = 0; entry < cars; entry++) {
		if (index == 0)
			goto invalid;

		order[--index].name = 1;
		order[index].source = entry;

		for (mesh = 1 + MH_CAR_MESHES * entry;
			mesh < 1 + MH_CAR_MESHES * (entry + 1); mesh++) {
			sources = scene->assets.meshes[mesh].halo_count;

			if (sources > scene->visuals.lamps.count - slot ||
				sources > index)
				goto invalid;

			for (n = 0; n < sources; n++)
				order[--index].source = slot++;
		}
	}

	if (slot != scene->visuals.lamps.count)
		goto invalid;

	spans = mh_mesh_scene_span_count(&scene->assets.meshes[0]);
	visited = 0;

	for (n = 1; n < spans; n++) {
		if (!mh_mesh_scene_halo_span(&scene->assets.meshes[0], n - 1,
			&first, &sources) ||
			first > scenery_count ||
			sources > scenery_count - first || sources > index)
			goto invalid;

		visited += sources;

		for (slot = 0; slot < sources; slot++)
			order[--index].source =
				scene->visuals.lamps.world_count + first + slot;
	}

	if (index != 0 || visited != scenery_count)
		goto invalid;

	*output = order;
	*count = total;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

invalid:
	free(order);

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"inconsistent shared effect source spans");
}

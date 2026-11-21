#include <stdlib.h>

#include "engine/render/scene_gpu.h"

enum {
	/*
	 * Skids, particles, sky and shadows precede individually ordered
	 * effects.
	 */
	FIXED_EFFECT_PARTS = 4
};

enum mh_error_code
mh_race_scene_gpu_order(struct mh_renderer *renderer,
	const struct mh_race_scene *scene, unsigned int cars,
	unsigned int finish_count, struct mh_error *error_out)
{
	const struct mh_mesh *world;
	struct mh_renderer_scene_part *parts;
	struct mh_scene_effect_slot *effects;
	enum mh_error_code status;
	size_t count, spans, i, n, effect_count, car_meshes;
	int ok;

	if (scene == NULL || cars == 0 || cars > MH_RACE_MAX_CARS ||
		finish_count > MH_FINISH_SIGNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid GPU scene layout");

	world = &scene->assets.meshes[0];
	car_meshes = cars * MH_CAR_MESHES;
	spans = mh_mesh_scene_span_count(world);

	count = car_meshes + FIXED_EFFECT_PARTS + finish_count +
		(scene->visuals.lamps.count == 0 ? 1 : 0);

	if (spans == 0 || spans > (size_t)-1 - count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race scene spans");

	count += spans;
	status = mh_race_scene_order_effects(
		scene, cars, &effects, &effect_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (effect_count > (size_t)-1 - count) {
		free(effects);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many race effects");
	}

	count += effect_count;

	if (count > (size_t)-1 / sizeof(*parts)) {
		free(effects);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many race scene spans");
	}

	parts = (struct mh_renderer_scene_part *)calloc(count, sizeof(*parts));

	if (parts == NULL) {
		free(effects);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory ordering race scene");
	}

	/*
	 * Prepare the world first, then car roots and their four wheel
	 * children, then scenery in LWS load order.
	 */
	ok = mh_mesh_scene_span(world, 0, &parts[0].span);
	parts[0].model_role = MH_RENDER_MODEL_WORLD;
	n = 1;

	for (i = 1; i <= car_meshes; i++, n++) {
		parts[n].mesh = i;
		parts[n].active_lod = 1;
		parts[n].model_role = (i - 1) % MH_CAR_MESHES == 0
			? MH_RENDER_MODEL_ROOT
			: MH_RENDER_MODEL_CHILD;
	}

	for (i = 1; ok && i < spans; i++, n++)
		ok = mh_mesh_scene_span(world, i, &parts[n].span);

	for (i = 0; i < finish_count; i++, n++) {
		parts[n].mesh = car_meshes + 1 + MH_SCENE_GPU_EFFECT_MESHES + i;
		parts[n].active_lod = 1;
	}

	for (i = MH_SCENE_GPU_SKID_MESH; i <= MH_SCENE_GPU_SHADOW_MESH; i++)
		if (i != MH_SCENE_GPU_LAMP_MESH) {
			parts[n].mesh = car_meshes + 1 + i;
			parts[n++].active_lod = 1;
		}

	if (scene->visuals.lamps.count == 0) {
		parts[n].mesh = car_meshes + 1 + MH_SCENE_GPU_LAMP_MESH;
		parts[n++].active_lod = 1;
	}

	for (i = 0; i < effect_count; i++, n++) {
		parts[n].mesh = car_meshes + 1 +
			(effects[i].name ? MH_SCENE_GPU_NAME_MESH
					: MH_SCENE_GPU_LAMP_MESH);

		/*
		 * Active names are reversed; unused draws occupy the cleared
		 * tail.
		 */
		parts[n].span.draw_first = effects[i].name
			? (effects[i].source < cars
					? cars - 1 - effects[i].source
					: effects[i].source)
			: effects[i].source * MH_LAMP_DRAWS;

		parts[n].span.draw_count = effects[i].name ? 1 : MH_LAMP_DRAWS;
	}

	status = mh_error_set(
		error_out, MH_ERROR_INVALID, "missing race scene span");

	if (ok)
		status = mh_renderer_scene_order(renderer, parts, count)
			? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: mh_renderer_error(renderer, error_out);

	free(parts);
	free(effects);

	return status;
}

#include <stdlib.h>

#include "engine/render/shadow.h"
#include "engine/render/software_scene.h"

enum {
	/* The first world span and the car shadow precede each body. */
	FIRST_CAR_PART = 2,
	CAR_PARTS = MH_CAR_MESHES + 1
};

size_t
mh_race_scene_software_car_part(unsigned int car)
{
	return FIRST_CAR_PART + car * CAR_PARTS;
}

size_t
mh_race_scene_software_world_part(unsigned int cars, size_t span)
{
	return span == 0 ? 0 : cars * CAR_PARTS + span;
}

static void
shadow_part(struct mh_renderer_scene_part *part, size_t mesh, unsigned int car)
{
	part->mesh = mesh;
	part->span.draw_first = car;
	part->span.draw_count = 1;
	part->span.primitive_first = car * MH_SHADOW_QUADS;
	part->span.primitive_count = MH_SHADOW_QUADS;
}

enum mh_error_code
mh_race_scene_software_order(struct mh_renderer *renderer,
	const struct mh_mesh *world, unsigned int cars,
	unsigned int finish_count, struct mh_error *error_out)
{
	struct mh_renderer_scene_part *parts;
	size_t spans, count, i, n, shadow_mesh, finish_mesh;
	unsigned int car, part;
	int ok;

	if (world == NULL || cars == 0 || cars > MH_RACE_MAX_CARS ||
		finish_count > MH_FINISH_SIGNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software scene layout");

	shadow_mesh = 1 + cars * MH_CAR_MESHES;
	finish_mesh = shadow_mesh + MH_SCENE_SOFTWARE_EFFECT_MESHES;
	spans = mh_mesh_scene_span_count(world);
	count = spans + shadow_mesh - 1 + MH_RACE_MAX_CARS + finish_count;

	if (spans == 0 || count < spans || count > (size_t)-1 / sizeof(*parts))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid software race scene spans");

	parts = calloc(count, sizeof(*parts));

	if (parts == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory ordering software race");

	ok = mh_mesh_scene_span(world, 0, &parts[0].span);
	parts[0].model_role = MH_RENDER_MODEL_WORLD;

	for (car = 0; car < cars; car++) {
		n = mh_race_scene_software_car_part(car) - 1;
		shadow_part(&parts[n++], shadow_mesh, car);

		for (part = 0; part < MH_CAR_MESHES; part++, n++) {
			parts[n].mesh = 1 + car * MH_CAR_MESHES + part;
			parts[n].active_lod = 1;
			parts[n].model_role = part == 0 ? MH_RENDER_MODEL_ROOT
							: MH_RENDER_MODEL_CHILD;
		}
	}

	for (i = 1; ok && i < spans; i++, n++) {
		n = mh_race_scene_software_world_part(cars, i);
		ok = mh_mesh_scene_span(world, i, &parts[n].span);
	}

	for (i = 0; i < finish_count; i++, n++) {
		parts[n].mesh = finish_mesh + i;
		parts[n].active_lod = 1;
	}

	/*
	 * Keep every fixed shadow span covered, including unused hidden slots.
	 */
	for (car = cars; car < MH_RACE_MAX_CARS; car++)
		shadow_part(&parts[n++], shadow_mesh, car);

	if (ok)
		ok = mh_renderer_scene_order(renderer, parts, count);

	free(parts);

	if (!ok)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"software race scene order rejected");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

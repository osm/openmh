#include <stdio.h>
#include <string.h>

#include "engine/render/mesh_internal.h"
#include "engine/render/openmh_scene_lighting.h"
#include "engine/render/renderer.h"
#include "engine/render/scene_gpu.h"
#include "engine/render/software_scene.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

#define PART_CAPACITY 128

static struct mh_race_scene scene;
static struct mh_field field;
static struct mh_renderer renderer;
static struct mh_render_backend backend;
static struct mh_render_ops ops;
static struct mh_renderer_scene_part recorded[PART_CAPACITY];
static size_t recorded_count, light_mesh, light_root;
static struct mh_model_lighting light;
static const struct mh_world *light_world;
static int reject_order;

static int
record_order(struct mh_renderer *unused,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	(void)unused;

	if (reject_order || count > PART_CAPACITY) {
		mh_error_set(error_out, MH_ERROR_FAILED, "rejected test order");
		return 0;
	}

	memcpy(recorded, parts, count * sizeof(*parts));
	recorded_count = count;

	return 1;
}

static enum mh_error_code
record_light(struct mh_renderer *unused, size_t mesh, size_t root,
	const struct mh_world *world, const struct mh_model_lighting *lighting,
	struct mh_error *error_out)
{
	(void)unused;

	light_mesh = mesh;
	light_root = root;
	light_world = world;

	if (lighting != NULL)
		light = *lighting;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_gpu_order(unsigned int cars, unsigned int finish)
{
	size_t n, i, effects;
	unsigned int names;

	CHECK(mh_race_scene_gpu_order(&renderer, &scene, cars, finish, NULL) ==
		MH_ERROR_NONE);
	CHECK(recorded[0].mesh == 0 &&
		recorded[0].model_role == MH_RENDER_MODEL_WORLD);
	n = 1;

	for (i = 1; i <= cars * MH_CAR_MESHES; i++, n++) {
		CHECK(recorded[n].mesh == i && recorded[n].active_lod);
		CHECK(recorded[n].model_role ==
			((i - 1) % MH_CAR_MESHES == 0 ? MH_RENDER_MODEL_ROOT
						: MH_RENDER_MODEL_CHILD));
	}

	CHECK(recorded[n].mesh == 0 && recorded[n].span.draw_first == 1);
	n++;
	effects = 1 + cars * MH_CAR_MESHES;

	for (i = 0; i < finish; i++, n++)
		CHECK(recorded[n].mesh == effects + 6 + i);

	/* Skid, particle, sky, shadow, then the empty lamp placeholder. */
	CHECK(recorded[n].mesh == effects);
	n++;
	CHECK(recorded[n].mesh == effects + 1);
	n++;
	CHECK(recorded[n].mesh == effects + 2);
	n++;
	CHECK(recorded[n].mesh == effects + 4);
	n++;
	CHECK(recorded[n].mesh == effects + 3);
	n++;
	names = 0;

	for (i = 0; i < MH_NAME_LABELS_MAX; i++, n++) {
		CHECK(recorded[n].mesh == effects + 5);
		CHECK(recorded[n].span.draw_count == 1);
		CHECK(recorded[n].span.draw_first < MH_NAME_LABELS_MAX);

		if (i < cars)
			CHECK(recorded[n].span.draw_first == i);

		names |= 1U << recorded[n].span.draw_first;
	}

	CHECK(names == (1U << MH_NAME_LABELS_MAX) - 1);
	CHECK(recorded_count == n);

	return 0;
}

static int
check_order(void)
{
	struct mh_mesh_world world;
	struct mh_mesh_scenery scenery;
	struct mh_mesh_binding binding;
	struct mh_renderer_name names[MH_NAME_LABELS_MAX];
	float models[MH_FIELD_MESHES * 16];
	size_t count, i, n, shadow_mesh, finish_mesh;
	unsigned int cars, finish, part;

	memset(&world, 0, sizeof(world));
	memset(&scenery, 0, sizeof(scenery));
	memset(&binding, 0, sizeof(binding));
	memset(models, 0, sizeof(models));
	world.scene_count = 1;
	world.span.draw_count = world.span.primitive_count = 1;
	binding.draw_first = binding.primitive_first = 1;
	binding.draw_count = binding.primitive_count = 1;
	scenery.bindings = &binding;
	scene.assets.meshes[0].world = &world;
	scene.assets.meshes[0].scenery = &scenery;
	scene.assets.meshes[0].draw_count = 2;
	scene.assets.meshes[0].primitive_count = 2;

	for (cars = 1; cars <= MH_RACE_MAX_CARS; cars++) {
		field.count = cars;
		field.active_mask = (1U << cars) - 1;
		field.cars[0].camera_mode = MH_CAMERA_TRACK;
		scene.assets.software.names.count = cars;
		scene.assets.software.names.active_mask = field.active_mask;

		for (finish = 0; finish <= MH_FINISH_SIGNS; finish++) {
			CHECK(mh_race_scene_software_order(&renderer,
				&scene.assets.meshes[0], cars, finish,
				NULL) == MH_ERROR_NONE);
			CHECK(recorded_count ==
				2 + cars * MH_CAR_MESHES + MH_RACE_MAX_CARS +
					finish);
			CHECK(recorded[0].mesh == 0 &&
				recorded[0].model_role ==
					MH_RENDER_MODEL_WORLD);
			CHECK(recorded[0].span.draw_first == 0 &&
				recorded[0].span.draw_count == 1);
			shadow_mesh = 1 + cars * MH_CAR_MESHES;
			finish_mesh = shadow_mesh + 1;
			n = 1;

			for (i = 0; i < cars; i++) {
				CHECK(recorded[n].mesh == shadow_mesh);
				CHECK(recorded[n].span.draw_first == i &&
					recorded[n].span.draw_count == 1);
				n++;

				for (part = 0; part < MH_CAR_MESHES;
					part++, n++) {
					CHECK(recorded[n].mesh ==
						1 + i * MH_CAR_MESHES + part);
					CHECK(recorded[n].active_lod);
					CHECK(recorded[n].model_role ==
						(part == 0 ? MH_RENDER_MODEL_ROOT
							: MH_RENDER_MODEL_CHILD));
				}
			}

			CHECK(recorded[n].mesh == 0 &&
				recorded[n].span.draw_first == 1);
			n++;

			for (i = 0; i < finish; i++, n++)
				CHECK(recorded[n].mesh == finish_mesh + i &&
					recorded[n].active_lod);

			for (i = cars; i < MH_RACE_MAX_CARS; i++, n++) {
				CHECK(recorded[n].mesh == shadow_mesh);
				CHECK(recorded[n].span.draw_first == i &&
					recorded[n].span.draw_count == 1);
			}

			CHECK(n == recorded_count);

			CHECK(mh_race_scene_software_names(&scene, &field,
				models, field.active_mask, 1, names,
				&count, NULL) == MH_ERROR_NONE);
			CHECK(count == cars);

			for (i = 0; i < count; i++) {
				CHECK(names[i].part < recorded_count);
				CHECK(recorded[names[i].part].mesh ==
					1 + i * MH_CAR_MESHES);
				CHECK(recorded[names[i].part].model_role ==
					MH_RENDER_MODEL_ROOT);
				CHECK(names[i].model ==
					models +
						recorded[names[i].part].mesh *
							16);
			}

			CHECK(check_gpu_order(cars, finish) == 0);
		}
	}

	CHECK(mh_race_scene_software_order(&renderer, &scene.assets.meshes[0],
		0, 0, NULL) == MH_ERROR_INVALID);
	CHECK(mh_race_scene_software_order(&renderer, &scene.assets.meshes[0],
		MH_RACE_MAX_CARS + 1, 0, NULL) == MH_ERROR_INVALID);
	CHECK(mh_race_scene_software_order(&renderer, &scene.assets.meshes[0],
		1, MH_FINISH_SIGNS + 1, NULL) == MH_ERROR_INVALID);
	reject_order = 1;
	CHECK(mh_race_scene_software_order(&renderer, &scene.assets.meshes[0],
		1, 0, NULL) == MH_ERROR_FAILED);
	reject_order = 0;
	scenery.bindings = NULL;
	CHECK(mh_race_scene_software_order(&renderer, &scene.assets.meshes[0],
		1, 0, NULL) == MH_ERROR_FAILED);
	scene.assets.meshes[0].world = NULL;
	scene.assets.meshes[0].scenery = NULL;

	return 0;
}

static int
check_lighting(void)
{
	struct mh_finish_frame frame;
	unsigned int i;

	field.count = 1;
	scene.assets.track.light_intensity = 2;
	scene.assets.track.object_ambient[0] = .5;
	scene.visuals.brightness.object[0] = .75;
	field.cars[0].eye[0] = 9;

	for (i = 0; i <= MH_CAR_MESHES; i++) {
		CHECK(mh_openmh_scene_lighting(&scene, &field.cars[0],
			&renderer, i, 1, NULL) == MH_ERROR_NONE);
		CHECK(light_mesh == i && light_root == (i == 0 ? 0 : 1));
		CHECK(light.mode == (i == 1 ? MH_LIGHT_CAR : MH_LIGHT_OBJECT));
		CHECK(light.eye[0] == 9 && light.intensity == 2);
		CHECK(light.ambient[0] == .5 && light.brightness[0] == .75);
		CHECK(light_world == &scene.assets.lighting_world);
	}

	CHECK(mh_openmh_scene_lighting(&scene, &field.cars[0], &renderer, 1, 0,
		NULL) == MH_ERROR_NONE &&
		light_world == NULL);

	memset(&frame, 0, sizeof(frame));
	frame.rank = 2;
	frame.color[0] = .5f;
	scene.assets.profile = MH_RENDER_SOFTWARE;
	scene.assets.lighting_world.has_directional = 1;
	scene.assets.lighting_world.directional.color[0] = 255;
	scene.assets.lighting_world.directional.intensity = 1;
	CHECK(mh_race_scene_finish_lighting(&scene, &field, &renderer, 20,
		&frame, 0, NULL) == MH_ERROR_NONE);
	CHECK(light_mesh == 21 && light_root == 21);
	CHECK(light.ambient[0] == .25f);
	CHECK(light.directional_color[0] > .49999f &&
		light.directional_color[0] < .50001f);
	frame.rank = 0;
	CHECK(mh_race_scene_finish_lighting(&scene, &field, &renderer, 20,
		&frame, 0, NULL) == MH_ERROR_INVALID);

	return 0;
}

int
main(void)
{
	ops.scene_order = record_order;
	ops.world_lighting = record_light;
	backend.ops = &ops;
	renderer.backend = &backend;
	CHECK(check_order() == 0);
	CHECK(check_lighting() == 0);

	return 0;
}

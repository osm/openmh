#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/geometry_source.h"
#include "engine/geometry/load.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
make_templates(struct mh_test_geometry_source *source)
{
	struct mh_model *model;
	struct mh_world *world;

	memset(source, 0, sizeof(*source));
	source->model_count = source->world_count = 1;
	model = &source->models[0];
	model->vertex_count = 3;
	model->face_count = model->texture_count = model->halo_count = 1;
	model->faces = calloc(1, sizeof(*model->faces));
	model->halos = calloc(1, sizeof(*model->halos));
	model->textures = calloc(1, sizeof(*model->textures));
	CHECK(model->faces != NULL && model->halos != NULL &&
		model->textures != NULL);
	model->textures[0] = malloc(sizeof("paint.texture"));
	CHECK(model->textures[0] != NULL);
	strcpy(model->textures[0], "paint.texture");
	model->faces[0].count = 3;
	model->faces[0].corners[1].position[0] = 1;
	model->faces[0].corners[2].position[2] = 1;
	model->faces[0].plane[1] = 1;
	model->halos[0].resource = 7;
	model->radius = 2;

	world = &source->worlds[0];
	world->width = world->height = world->cell_count = 1;
	world->cell_size[0] = world->cell_size[1] = 10;
	world->light_count = world->link_count = 1;
	world->cells = calloc(1, sizeof(*world->cells));
	world->lights = calloc(1, sizeof(*world->lights));
	world->links = calloc(1, sizeof(*world->links));
	CHECK(world->cells != NULL && world->lights != NULL &&
		world->links != NULL);
	world->cells[0].model = MH_WORLD_NO_MODEL;
	world->lights[0].radius = 4;
	world->lights[0].intensity = 0.5;
	world->links[0].next = MH_WORLD_NO_LIGHT;

	return 0;
}

int
main(void)
{
	static const unsigned char key[2] = {0, 0};
	static const unsigned char missing[2] = {1, 0};
	static const struct mh_resource_entry entries[] = {
		{"memory/Models/Car.fixturemodel", key, sizeof(key)},
		{"memory/Tracks/Road.fixtureworld", key, sizeof(key)}};

	struct mh_test_geometry_source source;
	struct mh_model_decoder models;
	struct mh_world_decoder worlds;
	const struct mh_resource_source *memory;
	struct mh_model model, saved_model;
	struct mh_world world, saved_world;
	struct mh_point_light light;
	struct mh_error error;
	const char *extension;
	char path[128];
	double position[3] = {1, 0, 1};
	size_t cursor;
	int found;

	CHECK(make_templates(&source) == 0);
	mh_test_geometry_decoders(&source, &models, &worlds);
	CHECK(mh_memory_resources_open(entries, 2, &memory, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_model_load_from(&models, memory, "memory", "models\\car.MYO",
		&model, &error) == MH_ERROR_NONE);
	CHECK(mh_world_load_from(&worlds, memory, "memory", "tracks\\road.MYW",
		&world, &error) == MH_ERROR_NONE);
	CHECK(source.model_reads == 1 && source.world_reads == 1);
	CHECK(model.faces != source.models[0].faces &&
		model.textures != source.models[0].textures);
	CHECK(model.textures[0] != source.models[0].textures[0]);
	CHECK(world.lights != source.worlds[0].lights);
	saved_model = model;
	saved_world = world;
	CHECK(mh_model_load_from(&models, memory, "memory", "missing.myo",
		&model, &error) == MH_ERROR_NOT_FOUND);
	CHECK(mh_world_load_from(&worlds, memory, "memory", "missing.myw",
		&world, &error) == MH_ERROR_NOT_FOUND);
	CHECK(mh_model_path_find_from(&models, memory, "memory",
		"Models/Car1.myo", path, sizeof(path), &found,
		&error) == MH_ERROR_NONE &&
		!found);
	CHECK(mh_model_decode(&models, missing, sizeof(missing), &model,
		&error) == MH_ERROR_INVALID);
	CHECK(mh_world_decode(&worlds, missing, sizeof(missing), &world,
		&error) == MH_ERROR_INVALID);
	CHECK(mh_model_decode(NULL, key, sizeof(key), &model, &error) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_world_decode(NULL, key, sizeof(key), &world, &error) ==
		MH_ERROR_UNSUPPORTED);
	models.extension = "../bad";
	worlds.extension = "";
	CHECK(mh_model_load_from(&models, memory, "memory", "Models/Car.myo",
		&model, &error) == MH_ERROR_INVALID);
	CHECK(mh_world_load_from(&worlds, memory, "memory", "Tracks/Road.myw",
		&world, &error) == MH_ERROR_INVALID);
	models.extension = NULL;
	CHECK(mh_model_decoder_extension(&models, &extension, &error) ==
			MH_ERROR_NONE &&
		extension == NULL);
	CHECK(mh_model_load_from(&models, memory, "memory", "Models/Car.myo",
		&model, &error) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&model, &saved_model, sizeof(model)) == 0);
	CHECK(memcmp(&world, &saved_world, sizeof(world)) == 0);
	CHECK(source.model_reads == 1 && source.world_reads == 1);
	mh_memory_resources_close(memory);
	mh_test_geometry_source_free(&source);
	memset(&models, 0, sizeof(models));
	memset(&worlds, 0, sizeof(worlds));
	CHECK(strcmp(model.textures[0], "paint.texture") == 0);
	CHECK(model.faces[0].corners[1].position[0] == 1);
	CHECK(model.halos[0].resource == 7 && model.radius == 2);
	CHECK(mh_world_lights_begin(&world, position, &cursor, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_world_light_next(&world, &cursor, &light));
	CHECK(light.radius == 4 && light.intensity == 0.5);
	CHECK(!mh_world_light_next(&world, &cursor, &light));
	mh_model_free(&model);
	mh_world_free(&world);
	puts("Selected geometry loading, independent extensions, failure atomicity and ownership passed");

	return 0;
}

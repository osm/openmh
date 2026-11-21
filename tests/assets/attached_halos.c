#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"

static struct mh_race_context race_context;
static struct mh_field field;
static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int
has_glow(size_t index)
{
	const struct mh_vertex *vertex;

	vertex = race_context.scene.visuals.lamps.mesh.vertices + index * 54 +
		42;

	return vertex->color[0] > 0 || vertex->color[1] > 0 ||
		vertex->color[2] > 0;
}

static void
check_scenery_sources(void)
{
	static const double times[] = {0, .25, 1, 5, 15};
	struct mh_av_settings settings;
	struct mh_geometry_halo halo;
	struct mh_lamps *lamps;
	struct mh_mesh *world;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16], tint[3],
		center[3];
	size_t n, count, index, queries, glows;
	unsigned int sample, option, axis, vertex;

	lamps = &race_context.scene.visuals.lamps;
	world = &race_context.scene.assets.meshes[0];

	CHECK(mh_av_settings_init(&settings, 0, 0, NULL) == MH_ERROR_NONE);
	settings.halos = 1;
	race_context.settings = &settings;
	mh_field_matrices(&field, 1.5, models, matrix, view);
	count = mh_mesh_scene_halo_count(world);
	queries = glows = 0;

	for (sample = 0; sample < sizeof(times) / sizeof(times[0]); sample++) {
		CHECK(mh_mesh_animate(world, times[sample], NULL) ==
			MH_ERROR_NONE);

		for (n = 0; n < count; n++) {
			CHECK(mh_mesh_scene_halo(world, n, &halo, tint));

			for (axis = 0; axis < 3; axis++)
				center[axis] =
					(halo.endpoints[0][axis] +
						halo.endpoints[1][axis]) *
					.5f;

			/*
			 * Inspect each real, animated source with a camera
			 * centered on it. The production parent gate, tint and
			 * style still run.
			 */
			memset(matrix, 0, sizeof(matrix));
			memset(view, 0, sizeof(view));
			matrix[0] = view[0] = 1;
			matrix[5] = 1.5f;
			view[5] = 1;
			matrix[10] = matrix[11] = 1;
			view[10] = -1;
			view[15] = 1;
			matrix[12] = view[12] = -center[0];
			matrix[13] = -1.5f * center[1];
			view[13] = -center[1];
			matrix[15] = 20 - center[2];
			view[14] = center[2] - 20;
			memset(&field.cars[0].camera, 0,
				sizeof(field.cars[0].camera));
			field.cars[0].camera.rotation[0] =
				field.cars[0].camera.rotation[4] =
					field.cars[0].camera.rotation[8] = 1;

			for (axis = 0; axis < 3; axis++)
				field.cars[0].camera.position[axis] =
					center[axis];

			field.cars[0].camera.position[2] -= 20;
			index = lamps->world_count + n;

			for (option = 0; option < 4; option++) {
				settings.lens_flares = option & 1;
				lamps->visible[index] =
					(unsigned char)(option >> 1);
				CHECK(mh_race_scene_update_lamps(
					&race_context.scene, &field,
					models, matrix, view, 960, 640,
					race_context.settings,
					NULL) == MH_ERROR_NONE);
				CHECK(lamps->probes[index].active);
				queries += lamps->probes[index].active != 0;
				CHECK(lamps->visible[index] == (option >> 1));

				if (!(option >> 1))
					CHECK(!has_glow(index));

				glows += has_glow(index) != 0;

				for (vertex = 0; vertex < 42; vertex++)
					for (axis = 0; axis < 3; axis++)
						CHECK(lamps->mesh
								.vertices[index *
										54 +
									vertex]
								.color[axis] ==
							0);
			}
		}
	}

	CHECK(count == 0 || glows != 0);
	printf("scenery: %lu sources, %lu queries, %lu visible glows; no positive lens reflections\n",
		(unsigned long)count, (unsigned long)queries,
		(unsigned long)glows);
	race_context.settings = NULL;
}

int
main(int argc, char *argv[])
{
	struct mh_drive drive;
	struct mh_lamps *lamps;
	struct mh_mesh *car_mesh;
	struct mh_av_settings settings;
	struct mh_vertex kept[4 * 54];
	struct mh_depth_probe probes[4];
	struct mh_geometry_halo saved_halos[16];
	struct mh_error error;
	enum mh_error_code status;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	float old_green[4], old_width[4], width, radius, position[3];
	size_t base, index, n, count;
	unsigned int car, track, i, side, braking, gate, level, option, j,
		plane, axis, endpoint;
	int rear;

	if (argc != 4 &&
		!(argc == 5 &&
			(strcmp(argv[4], "--bounds") == 0 ||
				strcmp(argv[4], "--scenery") == 0)))
		return 2;

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	status = mh_race_context_load(
		argv[1], car, track, 1, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_prepare_lamps(
			&race_context.scene, &field, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		mh_race_context_free(&race_context);
		return 1;
	}

	lamps = &race_context.scene.visuals.lamps;
	car_mesh = &race_context.scene.assets.meshes[1];

	if (argc == 5 && strcmp(argv[4], "--scenery") == 0) {
		check_scenery_sources();
		mh_race_context_free(&race_context);
		return failures != 0;
	}

	if (argc == 5) {
		count = mh_mesh_scene_halo_count(
			&race_context.scene.assets.meshes[0]);

		for (n = 0; n < count; n++) {
			CHECK(mh_mesh_scene_halo_bounds(
				&race_context.scene.assets.meshes[0], n,
				position, &radius));
			printf("halo %lu %.9g\n", (unsigned long)n,
				(double)radius);
		}

		mh_race_context_free(&race_context);

		return failures != 0;
	}

	CHECK(car_mesh->lod_count == 4 && car_mesh->halo_count == 16);

	if (car_mesh->lod_count != 4 || car_mesh->halo_count != 16) {
		mh_race_context_free(&race_context);
		return 1;
	}

	count = mh_mesh_scene_halo_count(&race_context.scene.assets.meshes[0]);
	base = lamps->world_count + count;
	CHECK(lamps->count == base + 16);
	CHECK(mh_race_scene_prepare_lamps(&race_context.scene, &field, NULL) ==
		MH_ERROR_NONE);
	mh_field_matrices(&field, 1.5, models, matrix, view);

	/*
	 * Fixed camera fixtures isolate local-to-camera attachment from
	 * physics. All original car endpoints and the production policy/mesh
	 * path run.
	 */
	memset(models + 16, 0, 16 * sizeof(float));

	for (i = 0; i < 4; i++)
		models[16 + i * 5] = 1;

	memset(old_green, 0, sizeof(old_green));
	memset(old_width, 0, sizeof(old_width));

	for (level = 0; level < 4; level++) {
		car_mesh->lod = level;
		base = lamps->world_count + count +
			car_mesh->lods[level].halo_first;
		CHECK(car_mesh->lods[level].halo_count == 4);

		for (side = 0; side < 2; side++) {
			memset(matrix, 0, sizeof(matrix));
			memset(view, 0, sizeof(view));
			matrix[0] = view[0] = side ? -1 : 1;
			matrix[5] = 1.5f;
			view[5] = 1;
			matrix[10] = 1;
			matrix[11] = side ? -1 : 1;
			matrix[15] = 20;
			view[10] = side ? 1 : -1;
			view[14] = -20;
			view[15] = 1;
			memset(&field.cars[0].camera, 0,
				sizeof(field.cars[0].camera));
			field.cars[0].camera.rotation[0] =
				field.cars[0].camera.rotation[8] =
					side ? -1 : 1;
			field.cars[0].camera.rotation[4] = 1;
			field.cars[0].camera.position[2] = side ? 20 : -20;

			for (braking = 0; braking < 2; braking++) {
				field.cars[0].vehicle.brake_active =
					(int)braking;
				CHECK(mh_race_scene_update_lamps(
					&race_context.scene, &field,
					models, matrix, view, 960, 640,
					race_context.settings,
					NULL) == MH_ERROR_NONE);

				for (n = lamps->world_count + count;
					n < lamps->count; n++)
					if (n < base || n >= base + 4)
						CHECK(!has_glow(n) &&
							!lamps->probes[n]
								.active);

				for (n = 0; n < 4; n++) {
					index = base + n;
					i = (unsigned int)car_mesh->lods[level]
							.halo_first +
						(unsigned int)n;
					rear = car_mesh->halos[i].resource == 1;
					CHECK(car_mesh->halos[i].resource <= 1);
					CHECK(lamps->probes[index].active ==
						(side ? !rear : rear));
					CHECK(has_glow(index) ==
						(side ? !rear : rear));

					if (!side && rear) {
						width = (float)fabs(
							lamps->mesh
								.vertices[index *
										54 +
									44]
								.position[0] -
							lamps->mesh
								.vertices[index *
										54 +
									42]
								.position[0]);

						if (!braking) {
							old_width[n] = width;
							old_green[n] =
								lamps->mesh
									.vertices[index *
											54 +
										42]
									.color[1];
						} else
							CHECK(width > old_width[n] &&
								lamps->mesh.vertices[index *
											54 +
										42]
										.color[1] >
									old_green
										[n]);

						CHECK(lamps->mesh
								.vertices[index *
									54]
								.color[0] == 0);
					}

					if (side && !rear)
						CHECK(lamps->mesh
								.vertices[index *
									54]
								.color[0] > 0);
				}
			}

			/*
			 * All LODs and head/rear orientations retain their glow
			 * and depth query with LensFlares Off. Halos Off gates
			 * the complete source, including its query, while
			 * preserving previous visibility.
			 */
			memcpy(kept, lamps->mesh.vertices + base * 54,
				sizeof(kept));
			memcpy(probes, lamps->probes + base, sizeof(probes));
			CHECK(mh_av_settings_init(&settings, 0, 0, NULL) ==
				MH_ERROR_NONE);
			race_context.settings = &settings;

			for (option = 0; option < 4; option++) {
				settings.halos = option == 0 || option == 3;
				settings.lens_flares =
					option == 1 || option == 3;
				CHECK(mh_race_scene_update_lamps(
					&race_context.scene, &field,
					models, matrix, view, 960, 640,
					race_context.settings,
					NULL) == MH_ERROR_NONE);

				for (n = 0; n < 4; n++) {
					CHECK(lamps->visible[base + n] == 1);
					CHECK(lamps->probes[base + n].active ==
						(settings.halos &&
							probes[n].active));

					if (settings.halos)
						CHECK(memcmp(lamps->mesh.vertices +
								(base + n) *
									54 +
								42,
							kept + n * 54 +
								42,
							12 * sizeof(*kept)) ==
							0);
					else
						CHECK(!has_glow(base + n));

					if (option != 3)
						for (i = 0; i < 42; i++)
							for (j = 0; j < 3; j++)
								CHECK(lamps->mesh
										.vertices[(base + n) *
												54 +
											i]
										.color[j] ==
									0);
				}
			}

			CHECK(memcmp(kept, lamps->mesh.vertices + base * 54,
				sizeof(kept)) == 0);
			CHECK(memcmp(probes, lamps->probes + base,
				sizeof(probes)) == 0);
			race_context.settings = NULL;

			/*
			 * Hold all endpoints at their visible world positions
			 * while moving the root outside each plane. Endpoint
			 * clipping alone would still emit glows/queries. Root
			 * radius, independent of selected LOD, must gate them
			 * without consuming either visibility-history value.
			 */
			memcpy(saved_halos, car_mesh->halos,
				sizeof(saved_halos));
			radius = car_mesh->radius;

			for (plane = 0; plane < 6; plane++) {
				axis = plane / 2;
				models[28 + axis] = plane % 2 ? 1000 : -1000;

				for (n = 0; n < 16; n++)
					for (endpoint = 0; endpoint < 2;
						endpoint++)
						car_mesh->halos[n]
							.endpoints[endpoint]
								[axis] =
							saved_halos[n].endpoints
								[endpoint]
								[axis] -
							models[28 + axis];

				for (gate = 0; gate < 2; gate++) {
					for (n = base; n < base + 4; n++)
						lamps->visible[n] =
							(unsigned char)gate;

					CHECK(mh_race_scene_update_lamps(
						&race_context.scene,
						&field, models, matrix,
						view, 960, 640,
						race_context.settings,
						NULL) == MH_ERROR_NONE);

					for (n = base; n < base + 4; n++)
						CHECK(!has_glow(n) &&
							!lamps->probes[n]
								.active &&
							lamps->visible[n] ==
								gate);
				}

				car_mesh->radius = 2000;
				CHECK(mh_race_scene_update_lamps(
					&race_context.scene, &field,
					models, matrix, view, 960, 640,
					race_context.settings,
					NULL) == MH_ERROR_NONE);

				for (n = 0; n < 4; n++)
					CHECK(lamps->probes[base + n].active ==
						probes[n].active);

				car_mesh->radius = radius;
				models[28 + axis] = 0;
				memcpy(car_mesh->halos, saved_halos,
					sizeof(saved_halos));
			}

			CHECK(mh_race_scene_update_lamps(&race_context.scene,
				&field, models, matrix, view, 960, 640,
				race_context.settings,
				NULL) == MH_ERROR_NONE);
			CHECK(memcmp(kept, lamps->mesh.vertices + base * 54,
				sizeof(kept)) == 0);
			CHECK(memcmp(probes, lamps->probes + base,
				sizeof(probes)) == 0);

			for (n = base; n < base + 4; n++)
				lamps->visible[n] = 0;

			CHECK(mh_race_scene_update_lamps(&race_context.scene,
				&field, models, matrix, view, 960, 640,
				race_context.settings,
				NULL) == MH_ERROR_NONE);

			for (n = base; n < base + 4; n++)
				CHECK(!has_glow(n));

			for (n = base; n < base + 4; n++)
				lamps->visible[n] = 1;

			for (gate = 0; gate < 3; gate++) {
				field.active_mask = gate == 0 ? 0 : 1;
				field.unavailable_mask = gate == 1 ? 1 : 0;
				field.cars[0].camera_mode = gate == 2
					? MH_CAMERA_IN_CAR
					: MH_CAMERA_CHASE;
				CHECK(mh_race_scene_update_lamps(
					&race_context.scene, &field,
					models, matrix, view, 960, 640,
					race_context.settings,
					NULL) == MH_ERROR_NONE);

				for (n = base; n < base + 4; n++)
					CHECK(!has_glow(n) &&
						!lamps->probes[n].active);
			}

			field.active_mask = 1;
			field.unavailable_mask = 0;
			field.cars[0].camera_mode = MH_CAMERA_CHASE;
		}
	}

	printf("car%u track%u: four detail levels, %lu scenery sources; direction, brakes, effect switches, parent spheres, separate histories and eligibility %s\n",
		car, track, (unsigned long)count,
		failures ? "FAILED" : "passed");
	mh_race_context_free(&race_context);

	return failures != 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_scene.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/effect_particles.h"
#include "engine/render/effect_shadows.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/camera.h"

static struct mh_race_context context;
static struct mh_field field;

#define COLOR_LIMIT (255.0f * 200.0f)

/* Offsets from the end of the scene mesh table. */
enum {
	NAME_MESH = 1,
	HALO_MESH,
	PARTICLE_MESH,
	SHADOW_MESH,
	SKY_MESH,
	EFFECT_MESHES = SKY_MESH
};

enum sky_case {
	PANORAMA,
	PLAIN_SKY,
	BLACK_SKY,
	WEATHER_SKY,
	UNDERWATER_SKY,
	RESTORE_PANORAMA,
	SKY_CASES
};

static int
world_colors_match(const struct mh_mesh *mesh, const double brightness[3])
{
	float expected;
	size_t i;
	unsigned int channel;

	if (mesh->material_rgb == NULL ||
		mesh->material_rgb_count != mesh->vertex_count)
		return 0;

	for (i = 0; i < mesh->world_vertex_count; i++)
		for (channel = 0; channel < 3; channel++) {
			expected =
				(float)((long double)mesh->world_colors[i * 3 +
						channel] *
					(float)brightness[channel]);

			if (expected < 0)
				expected = 0;

			if (expected > 255)
				expected = 255;

			if (mesh->material_rgb[i * 3 + channel] != expected)
				return 0;
		}

	return 1;
}

static enum mh_error_code
prepare_lighting(const char *root, const struct mh_av_settings *settings,
	struct mh_error *error_out)
{
	struct mh_race_scene *scene;
	struct mh_drive drive;
	struct mh_engine_input input;
	struct mh_mesh *mesh;
	const struct mh_car *opponents[7];
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	enum mh_error_code status;
	size_t n, fractional;
	unsigned int i, pass;

	scene = &context.scene;
	status = mh_race_scene_load_ai(root, scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&scene->assets.car,
			&scene->assets.world_collision,
			&scene->assets.materials, &scene->assets.route,
			&scene->assets.track, 0, 0, 0, &drive, error_out);

	for (i = 0; i < 7; i++)
		opponents[i] = &scene->assets.opponents[i];

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, opponents, 7,
			&scene->assets.ai_path, 1, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field, scene->assets.car_hulls,
			field.count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(scene, &field, error_out);

	memset(&input, 0, sizeof(input));

	for (i = 0; i < 60 && status == MH_ERROR_NONE; i++)
		status = mh_field_tick(&field, &input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_field_matrices(&field, 4.0 / 3, models, matrix, view);
	fractional = 0;

	/* Loading, pause dimming and restoration must retain unpacked RGB. */
	for (pass = 0; pass < 3; pass++) {
		status = mh_race_scene_pause(
			scene, pass == 1, settings, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mesh = &scene->assets.meshes[0];

		if (!world_colors_match(mesh, scene->visuals.brightness.world))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"PowerVR world lost raw source brightness");

		status = mh_mesh_material_light_scene(mesh,
			&scene->assets.lighting_world,
			scene->assets.track.object_ambient,
			scene->visuals.brightness.object,
			scene->assets.track.light_intensity, field.cars[0].eye,
			scene->visuals.prepared_lights,
			scene->assets.lighting_world.light_count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 1; i < 1 + field.count * MH_CAR_MESHES; i++) {
			status = mh_race_scene_shade_car(scene, &field, i,
				models, view, 0, pass != 1, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			mesh = &scene->assets.meshes[i];

			if (mesh->material_rgb == NULL ||
				mesh->material_rgb_count != mesh->vertex_count)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"PowerVR car lost raw material colors");

			for (n = 0; n < mesh->vertex_count * 3; n++) {
				if (!(mesh->material_rgb[n] >= 0 &&
					mesh->material_rgb[n] <=
						COLOR_LIMIT))
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"invalid PowerVR car color");

				fractional += mesh->material_rgb[n] !=
					(float)(long)mesh->material_rgb[n];
			}
		}
	}

	if (fractional == 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"PowerVR lighting was quantized before submission");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
update_sky(unsigned int mode, const float matrix[16], const float view[16],
	float focal)
{
	struct mh_sky_state state;
	struct mh_weather_camera weather;
	struct mh_mesh *mesh;
	const struct mh_weather_style *style;
	unsigned int i;

	mesh = &context.scene.visuals.sky_mesh;
	memset(&state, 0, sizeof(state));

	if (mode == PLAIN_SKY)
		return mh_sky_mesh_plain(mesh, &state,
			&context.scene.assets.track, matrix, view, 320,
			240, NULL) == MH_ERROR_NONE;

	if (mode == BLACK_SKY)
		return mh_sky_mesh_black(mesh, NULL) == MH_ERROR_NONE;

	if (mode == WEATHER_SKY || mode == UNDERWATER_SKY) {
		memset(&weather, 0, sizeof(weather));
		weather.width = 320;
		weather.height = 240;
		weather.focal = focal;
		weather.underwater = mode == UNDERWATER_SKY;
		style = mh_water_style();
		weather.top = style->top;
		weather.bottom = style->bottom;

		for (i = 0; i < 3; i++) {
			weather.forward[i] = -view[i * 4 + 2];
			weather.position[i] = field.cars[0].eye[i];
		}

		return mh_sky_mesh_weather(mesh, &weather, NULL) ==
			MH_ERROR_NONE;
	}

	return mh_sky_mesh_update(mesh, &state, &context.scene.assets.track,
		matrix, view, 320, 240, NULL) == MH_ERROR_NONE;
}

/*
 * Controlled smoke/grass/spark pools exercise the shipped atlas in each scene.
 */
static int
update_particles(const float matrix[16], const struct mh_camera_lens *lens)
{
	struct mh_particle_camera camera;
	struct mh_particle_pool *pool;
	struct mh_particle *particle;
	const struct mh_drive *drive;
	unsigned int i, j;

	drive = &field.cars[0];
	memset(context.scene.visuals.particles, 0,
		sizeof(context.scene.visuals.particles));

	for (i = 0; i < 3; i++) {
		pool = &context.scene.visuals.particles[0].pools[i];
		pool->enabled = pool->count = 1;
		particle = &pool->particles[0];
		particle->active = 1;
		particle->flags = MH_PARTICLE_FADE;
		particle->style = i == 2 ? MH_PARTICLE_STYLE_SPARK
			: i == 0	 ? MH_PARTICLE_STYLE_SMOKE
					: 0;
		particle->animation = i == 0 ? MH_PARTICLE_SMOKE
			: i == 1	     ? MH_PARTICLE_GRASS
					: MH_PARTICLE_SPARK;
		particle->lifespan = particle->remaining = 1;
		particle->width = particle->height = .7f;

		for (j = 0; j < 3; j++) {
			particle->color[j] = .7f;
			particle->position[j] = (float)(drive->eye[j] +
				drive->camera.rotation[6 + j] * 6 +
				drive->camera.rotation[j] * ((int)i - 1) * 2);
		}
	}

	memset(&camera, 0, sizeof(camera));
	memcpy(camera.matrix, matrix, sizeof(camera.matrix));
	camera.width = 320;
	camera.height = 240;
	camera.near_depth = lens->near_depth;
	camera.far_depth = lens->far_depth;
	camera.brightness = 1;
	camera.smoke = camera.sparks = 1;

	return mh_particle_mesh_update(&context.scene.visuals.particle_mesh,
		context.scene.visuals.particles, MH_RACE_MAX_CARS,
		&camera, NULL) == MH_ERROR_NONE &&
		context.scene.visuals.particle_mesh.vertices[17].projection_w >
		0;
}

/* The start-grid camera can leave every opponent outside the label range. */
static enum mh_error_code
update_names(const float *models, const float view[16], int enabled,
	struct mh_error *error_out)
{
	float positions[MH_FIELD_MESHES * 16];
	const struct mh_drive *drive;
	unsigned int i, j, root;

	memcpy(positions, models, sizeof(positions));
	drive = &field.cars[0];

	for (i = 1; i < field.count; i++) {
		root = 1 + i * MH_CAR_MESHES;

		for (j = 0; j < 3; j++)
			positions[root * 16 + 12 + j] = (float)(drive->eye[j] +
				drive->camera.rotation[6 + j] * (12 + i * 2) +
				drive->camera.rotation[j] * ((int)(i % 3) - 1) *
					3);
	}

	return mh_race_scene_names(&context.scene, &field, positions, view, 320,
		240, enabled, error_out);
}

static int
check_effect_visible(struct mh_gpu_renderer *gpu, size_t mesh, const char *name,
	const struct mh_projection *projection, const float view[16],
	const float *models)
{
	struct mh_file visible, hidden;
	int ok;

	memset(&visible, 0, sizeof(visible));
	memset(&hidden, 0, sizeof(hidden));
	ok = mh_gpu_renderer_tga_capture(gpu, &visible) &&
		mh_gpu_renderer_mesh_visible(gpu, mesh, 0) &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 1, NULL, NULL) &&
		mh_gpu_renderer_tga_capture(gpu, &hidden);

	if (ok)
		ok = visible.size != hidden.size ||
			memcmp(visible.data, hidden.data, visible.size) != 0;

	free(visible.data);
	free(hidden.data);

	if (!ok)
		return SDL_SetError(
			"%s did not change the captured scene", name);

	return mh_gpu_renderer_mesh_visible(gpu, mesh, 1) &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 1, NULL, NULL);
}

static int
set_effect_order(struct mh_gpu_renderer *gpu)
{
	struct mh_renderer_scene_part *parts;
	struct mh_scene_effect_slot *effects;
	struct mh_error error;
	size_t count, effect_count, i, n;
	int ok;

	if (mh_race_scene_order_effects(&context.scene, field.count, &effects,
		&effect_count, &error) != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	count = gpu->mesh_count - 2 + effect_count +
		(context.scene.visuals.lamps.count == 0);
	parts = (struct mh_renderer_scene_part *)calloc(count, sizeof(*parts));

	if (parts == NULL) {
		free(effects);
		return 0;
	}

	for (n = 0; n + 2 < gpu->mesh_count; n++) {
		parts[n].mesh = n;
		parts[n].active_lod = 1;
	}

	if (context.scene.visuals.lamps.count == 0) {
		parts[n].mesh = gpu->mesh_count - HALO_MESH;
		parts[n++].active_lod = 1;
	}

	for (i = 0; i < effect_count; i++) {
		parts[n].mesh = gpu->mesh_count -
			(effects[i].name ? NAME_MESH : HALO_MESH);
		parts[n].span.draw_first = effects[i].name
			? (effects[i].source < field.count
					? field.count - 1 - effects[i].source
					: effects[i].source)
			: effects[i].source * 2;

		parts[n++].span.draw_count = effects[i].name ? 1 : 2;
	}

	ok = n == count && mh_gpu_renderer_scene_order(gpu, parts, count);
	free(parts);
	free(effects);

	return ok;
}

static int
check_reflection_frames(struct mh_gpu_renderer *gpu,
	const struct mh_projection *projection, const float view[16],
	const float *models)
{
	struct mh_file enabled, disabled, restored;
	int ok;

	memset(&enabled, 0, sizeof(enabled));
	memset(&disabled, 0, sizeof(disabled));
	memset(&restored, 0, sizeof(restored));
	ok = gpu->list_heads[MH_LIST_ENVIRONMENT] != MH_NO_PRIMITIVE &&
		mh_gpu_renderer_tga_capture(gpu, &enabled) &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 0, NULL, NULL) &&
		gpu->list_heads[MH_LIST_ENVIRONMENT] == MH_NO_PRIMITIVE &&
		mh_gpu_renderer_tga_capture(gpu, &disabled);

	if (ok)
		ok = enabled.size != disabled.size ||
			memcmp(enabled.data, disabled.data, enabled.size) != 0;

	ok = ok &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 1, NULL, NULL) &&
		gpu->list_heads[MH_LIST_ENVIRONMENT] != MH_NO_PRIMITIVE &&
		mh_gpu_renderer_tga_capture(gpu, &restored);

	if (ok)
		ok = enabled.size == restored.size &&
			memcmp(enabled.data, restored.data, enabled.size) == 0;

	free(enabled.data);
	free(disabled.data);
	free(restored.data);

	if (!ok)
		return SDL_SetError(
			"PowerVR reflection toggle/restoration failed");

	return 1;
}

static int
check_unavailable_cars(struct mh_gpu_renderer *gpu,
	const struct mh_projection *projection, const float view[16],
	const float *models)
{
	struct mh_race_scene *scene;
	struct mh_mesh *mesh;
	struct mh_error error;
	struct mh_file normal, unavailable, restored;
	enum mh_error_code status;
	size_t i, j;
	int ok;

	scene = &context.scene;
	memset(&normal, 0, sizeof(normal));
	memset(&unavailable, 0, sizeof(unavailable));
	memset(&restored, 0, sizeof(restored));
	ok = mh_gpu_renderer_tga_capture(gpu, &normal);

	for (i = 1; ok && i < 1 + field.count * MH_CAR_MESHES; i++) {
		status = mh_race_scene_shade_unavailable(
			scene, &field, (unsigned int)i, models, view, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Unavailable lighting: %s\n",
				error.message);
			ok = 0;
			break;
		}

		mesh = &scene->assets.meshes[i];

		for (j = 0; j < mesh->vertex_count * 3; j++)
			if (!(mesh->material_rgb[j] == 0 ||
				(mesh->material_rgb[j] >= 5 &&
					mesh->material_rgb[j] <= 70))) {
				ok = SDL_SetError(
					"PowerVR unavailable raw lighting was not updated");
				break;
			}

		ok = ok &&
			mh_gpu_renderer_mesh_unavailable(
				gpu, i, scene->visuals.shaded_vertices[i]);
	}

	ok = ok &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 1, NULL, NULL) &&
		gpu->list_heads[MH_LIST_UNAVAILABLE] != MH_NO_PRIMITIVE &&
		mh_gpu_renderer_tga_capture(gpu, &unavailable);

	if (ok)
		ok = normal.size != unavailable.size ||
			memcmp(normal.data, unavailable.data, normal.size) != 0;

	for (i = 1; ok && i < 1 + field.count * MH_CAR_MESHES; i++) {
		status = mh_race_scene_shade_car(scene, &field, (unsigned int)i,
			models, view, 0, 1, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Restored lighting: %s\n",
				error.message);
			ok = 0;
			break;
		}

		ok = mh_gpu_renderer_mesh_lit(
			gpu, i, scene->visuals.shaded_vertices[i]);
	}

	ok = ok &&
		mh_gpu_renderer_scene_frame_lens(
			gpu, projection, view, models, 1, NULL, NULL) &&
		gpu->list_heads[MH_LIST_UNAVAILABLE] == MH_NO_PRIMITIVE &&
		mh_gpu_renderer_tga_capture(gpu, &restored);

	if (ok)
		ok = normal.size == restored.size &&
			memcmp(normal.data, restored.data, normal.size) == 0;

	free(normal.data);
	free(unavailable.data);
	free(restored.data);

	if (!ok)
		return SDL_SetError(
			"PowerVR unavailable-car transition failed");

	return 1;
}

static int
check_scene(struct mh_gpu_renderer *gpu, const char *capture)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	struct mh_error error;
	struct mh_camera_lens lens;
	struct mh_projection projection;
	float models[(MH_FIELD_MESHES + EFFECT_MESHES) * 16], matrix[16],
		view[16];
	size_t i;
	unsigned int mode;

	mh_field_matrices(&field, 4.0 / 3, models, matrix, view);

	for (i = gpu->mesh_count - SKY_MESH; i < gpu->mesh_count; i++)
		memcpy(models + i * 16, identity, sizeof(identity));

	if (mh_camera_lens_setup(MH_CAMERA_RACE_FOCAL, MH_CAMERA_RACE_NEAR,
		mh_drive_camera_far(&field.cars[0]), 4.0f / 3, &lens,
		NULL) != MH_ERROR_NONE ||
		mh_projection_setup(320, 240, 4.0f / 3, lens.focal,
			lens.near_depth, lens.far_depth, &projection,
			NULL) != MH_ERROR_NONE)
		return 0;

	for (i = 0; i + EFFECT_MESHES < gpu->mesh_count; i++)
		if (!mh_gpu_renderer_mesh_lit(gpu, i,
			i == 0 ? context.scene.assets.meshes[0].vertices
				: context.scene.visuals.shaded_vertices[i]))
			return 0;

	if (mh_shadow_mesh_update(&context.scene.visuals.shadow_mesh, &field,
		models, matrix, field.cars[0].eye,
		&error) != MH_ERROR_NONE) {
		fprintf(stderr, "Shadow update: %s\n", error.message);
		return 0;
	}

	if (!mh_gpu_renderer_mesh_shadows(gpu, gpu->mesh_count - SHADOW_MESH))
		return 0;

	if (!update_particles(matrix, &lens) ||
		!mh_gpu_renderer_mesh_particles(
			gpu, gpu->mesh_count - PARTICLE_MESH))
		return 0;

	if (mh_race_scene_update_lamps(&context.scene, &field, models, matrix,
		view, 320, 240, NULL, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "Lamp update: %s\n", error.message);
		return 0;
	}

	for (i = 0; i < context.scene.visuals.lamps.count; i++)
		if (context.scene.visuals.lamps.probes[i].active)
			return SDL_SetError(
				"PowerVR generated a lamp depth query");

	if (update_names(models, view, 1, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "Name update: %s\n", error.message);
		return 0;
	}

	if (!mh_gpu_renderer_mesh_halos(gpu, gpu->mesh_count - NAME_MESH) ||
		!mh_gpu_renderer_mesh_halos(gpu, gpu->mesh_count - HALO_MESH))
		return 0;

	/*
	 * Reuse one upload for panorama, plain, black, weather and restoration.
	 */
	for (mode = 0; mode < SKY_CASES; mode++)
		if (!update_sky(mode, matrix, view, lens.focal) ||
			!mh_gpu_renderer_mesh_sky(
				gpu, gpu->mesh_count - SKY_MESH) ||
			!mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				view, models, 1, NULL, NULL) ||
			gpu->prepared_count == 0)
			return 0;

	if (gpu->list_heads[MH_LIST_SHADOW] == MH_NO_PRIMITIVE) {
		fprintf(stderr, "No shadow quads submitted\n");
		return 0;
	}

	return check_effect_visible(gpu, gpu->mesh_count - PARTICLE_MESH,
		"particles", &projection, view, models) &&
		check_effect_visible(gpu, gpu->mesh_count - HALO_MESH, "halos",
			&projection, view, models) &&
		check_effect_visible(gpu, gpu->mesh_count - NAME_MESH, "names",
			&projection, view, models) &&
		check_reflection_frames(gpu, &projection, view, models) &&
		check_unavailable_cars(gpu, &projection, view, models) &&
		(capture == NULL || mh_gpu_renderer_capture(gpu, capture));
}

int
main(int argc, char *argv[])
{
	struct mh_av_settings settings;
	struct mh_profile roster[MH_NAME_LABELS_MAX];
	struct mh_error error;
	struct mh_gpu_renderer *gpu;

	const struct mh_mesh
		*meshes[MH_DRIVE_MESHES + 7 * MH_CAR_MESHES + EFFECT_MESHES];
	const struct mh_mesh *mesh;
	enum mh_error_code status;
	unsigned int track;
	size_t i, j, slot, skipped, uploaded, count;
	int ok;

	if (argc != 3 && argc != 4)
		return 2;

	track = (unsigned int)atoi(argv[2]);

	if (track < 1 || track > 16)
		return 2;

	status = mh_av_settings_init(&settings, 0, 0, &error);
	settings.texture_precision = MH_AV_TEXTURE_32BIT;
	settings.texture_filter = MH_AV_FILTER_TRILINEAR;

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_profile(argv[1], 1, track, 1,
			&settings, MH_RENDER_POWERVR, NULL, &context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_add_opponents(
			argv[1], 1, track, 7, &context.scene, &error);

	if (status == MH_ERROR_NONE)
		status = prepare_lighting(argv[1], &settings, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_prepare_lamps(
			&context.scene, &field, &error);

	memset(roster, 0, sizeof(roster));

	for (i = 0; i < field.count; i++) {
		sprintf(roster[i].nick, "Driver %u", (unsigned int)i + 1);
		roster[i].colors[0] = i % 2 ? 0xff8040UL : 0x80c0ffUL;
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_names_prepare(argv[1], &context.scene,
			&field, roster, field.count, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		mh_race_context_free(&context);
		return 1;
	}

	count = sizeof(meshes) / sizeof(meshes[0]);

	for (i = 0; i + EFFECT_MESHES < count; i++)
		meshes[i] = &context.scene.assets.meshes[i];

	meshes[count - SKY_MESH] = &context.scene.visuals.sky_mesh;
	meshes[count - SHADOW_MESH] = &context.scene.visuals.shadow_mesh;
	meshes[count - PARTICLE_MESH] = &context.scene.visuals.particle_mesh;
	meshes[count - HALO_MESH] = &context.scene.visuals.lamps.mesh;
	meshes[count - NAME_MESH] = &context.scene.visuals.names.mesh;

	gpu = NULL;
	ok = SDL_Init(SDL_INIT_VIDEO);

	if (ok)
		gpu = mh_gpu_powervr_open(NULL, meshes, count, 320, 240, 0);

	ok = gpu != NULL && set_effect_order(gpu);

	if (ok)
		ok = gpu->meshes[count - HALO_MESH].images[0] ==
				gpu->meshes[count - SHADOW_MESH].images[1] &&
			gpu->meshes[count - HALO_MESH].images[1] ==
				gpu->meshes[count - SHADOW_MESH].images[1] &&
			gpu->meshes[count - NAME_MESH].images[0] ==
				gpu->meshes[count - SHADOW_MESH].images[1];

	skipped = uploaded = 0;

	for (i = 0; ok && i < count; i++) {
		mesh = meshes[i];

		for (j = 0; ok && j < mesh->draw_count; j++) {
			slot = gpu->meshes[i].images[mesh->draws[j].texture];
			ok = slot < gpu->image_count;
		}

		slot = gpu->meshes[i].images[mesh->environment_texture];
		ok = ok && slot < gpu->image_count;

		for (j = 0; j < mesh->texture_count; j++)
			skipped += gpu->meshes[i].images[j] == NO_IMAGE;
	}

	if (ok) {
		uploaded = gpu->image_count;

		for (i = 0; ok && i < uploaded; i++)
			ok = gpu->images[i].texture != NULL &&
				gpu->images[i].levels == 1;
	}

	if (ok)
		ok = check_scene(gpu, argc == 4 ? argv[3] : NULL);

	if (!ok)
		fprintf(stderr, "PowerVR ordinary scene: %s\n", SDL_GetError());

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();
	mh_race_context_free(&context);

	if (ok)
		printf("PowerVR track %u: %lu lit/sky/shadow/particle/halo/name meshes, six background modes, reflection/unavailable transitions, %lu shared uploads, %lu unused declarations skipped\n",
			track, (unsigned long)count, (unsigned long)uploaded,
			(unsigned long)skipped);

	return ok ? 0 : 1;
}

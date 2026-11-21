#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/retro/provider.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/internal.h"
#include "engine/geometry/world.h"
#include "interfaces/audio/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/window/backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "software race line %d: %s\n",         \
				__LINE__, #c);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(c)                                                                 \
	do {                                                                   \
		if ((c) != MH_ERROR_NONE) {                                    \
			fprintf(stderr, "software race line %d: %s\n",         \
				__LINE__, error.message);                      \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context context;
static struct mh_field field;
static struct mh_av_settings settings;
static unsigned int advance_calls;
static unsigned int shadow_frames, skid_frames, particle_frames;
static unsigned int world_halo_frames, car_halo_frames, scenery_halo_frames;
static unsigned int halo_picture_changes;

struct weather_case {
	unsigned int	flags;
	unsigned long	seed, next;
	double		rgb[3], fog[3];
	float		particle;
};

static void
count_effects(void)
{
	const struct mh_race_visuals *visuals;
	unsigned int car, i;
	size_t n;
	int shadow, skid, particle, world, car_light, scenery;

	visuals = &context.scene.visuals;
	shadow = skid = particle = 0;
	world = car_light = scenery = 0;

	for (car = 0; car < field.count; car++) {
		shadow |= !visuals->shadow_mesh.draw_hidden[car];

		for (i = 0; i < 4; i++)
			skid |= visuals->skids[car].wheels[i].count != 0;

		for (i = 0; i < MH_PARTICLE_POOLS; i++)
			particle |= visuals->particles[car].pools[i].count != 0;
	}

	for (n = 0; n < visuals->software_halos.count; n++) {
		world |= visuals->software_halos.sources[n].part == 0;
		car_light |= visuals->software_halos.sources[n].model != NULL;
		scenery |= visuals->software_halos.sources[n].tinted;
	}

	world_halo_frames += world;
	car_halo_frames += car_light;
	scenery_halo_frames += scenery;
	shadow_frames += shadow;
	skid_frames += skid;
	particle_frames += particle;
}

static int
capture_frame(struct mh_renderer *renderer, const char *path)
{
	struct mh_file picture;
	FILE *file;
	int success;

	CHECK(mh_renderer_tga_capture(renderer, &picture));
	file = fopen(path, "wb");
	success = file != NULL;

	if (success) {
		success = fwrite(picture.data, 1, picture.size, file) ==
			picture.size;

		if (fclose(file) != 0)
			success = 0;
	}

	free(picture.data);
	CHECK(success);

	return 1;
}

static enum mh_error_code
advance(void *opaque, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	(void)opaque;

	(void)elapsed;
	advance_calls++;
	*done = 0;

	return mh_field_advance_random(&field, controls, 1.0L / 30, action,
		steps, &context.scene.visuals.particle_system.random,
		error_out);
}

static int
load_race(const char *root, unsigned int track)
{
	const struct mh_car *roster[7];
	struct mh_drive drive;
	struct mh_error error;
	unsigned int i;

	TRY(mh_av_settings_init(&settings, 0, 0, &error));
	settings.width = 640;
	settings.height = 400;
	TRY(mh_race_context_load_profile(root, track, track, 1, &settings,
		MH_RENDER_SOFTWARE, NULL, &context, &error));
	TRY(mh_race_scene_load_ai(root, &context.scene, &error));
	TRY(mh_race_scene_add_opponents(
		root, track, track, 7, &context.scene, &error));
	TRY(mh_race_context_load_hud(root, &context, &error));
	TRY(mh_results_assets_load(root, 8, "Software",
		mh_race_context_atlas(&context), &context.results, &error));
	TRY(mh_drive_start(&context.scene.assets.car,
		&context.scene.assets.world_collision,
		&context.scene.assets.materials, &context.scene.assets.route,
		&context.scene.assets.track, 0, 0, 1, &drive, &error));
	TRY(mh_drive_enable_ai(
		&drive, &context.scene.assets.ai_path, 2, &error));

	for (i = 0; i < 7; i++)
		roster[i] = &context.scene.assets.opponents[i];

	TRY(mh_field_start(&drive, roster, 7, &context.scene.assets.ai_path, 2,
		&field, &error));
	TRY(mh_field_set_hulls(
		&field, context.scene.assets.car_hulls, field.count, &error));
	TRY(mh_race_scene_bind_objects(&context.scene, &field, &error));

	return 1;
}

/* Independent cell offsets from original model face counts. */
static int
world_halo_order(void)
{
	const struct mh_world *world;
	struct mh_world_cell cell;
	const struct mh_model *model;
	size_t n, offset, actual;

	world = &context.scene.assets.lighting_world;
	offset = world->face_count;

	for (n = world->cell_count; n > 0; n--) {
		cell = world->cells[n - 1];

		if (cell.model != MH_WORLD_NO_MODEL) {
			model = &world->models[cell.model].geometry;
			CHECK(offset >= model->face_count);
			offset -= model->face_count;
		}

		CHECK(mh_mesh_world_cell_primitive(
			&context.scene.assets.meshes[0], n - 1, &actual));
		CHECK(actual == offset);
	}

	CHECK(offset == 0);

	return 1;
}

static int
halo_settings(struct mh_race_loop *loop)
{
	struct mh_software_scene_halos *halos;
	const struct mh_renderer_halo *source, *previous;
	struct mh_world_view view;
	struct mh_file with_halos, without_halos;
	struct mh_error error;
	size_t n, count;
	unsigned int saved, car;

	halos = &context.scene.visuals.software_halos;
	CHECK(world_halo_order());
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &with_halos));
	count = halos->count;

	for (n = 0; n < count; n++) {
		source = &halos->sources[n];
		CHECK(source->lenses);

		if (n != 0) {
			previous = &halos->sources[n - 1];
			CHECK(previous->part < source->part ||
				(previous->part == source->part &&
					previous->before_primitive <=
						source->before_primitive));
		}

		if (source->model != NULL) {
			CHECK(source->part >= 2);
			CHECK((source->part - 2) % (MH_CAR_MESHES + 1) == 0);
			car = (unsigned int)((source->part - 2) /
				(MH_CAR_MESHES + 1));
			CHECK(car < field.count);
			CHECK(source->model ==
				loop->models + (1 + car * MH_CAR_MESHES) * 16);
		}
	}

	settings.lens_flares = 0;
	CHECK(mh_race_draw_frame(loop));
	CHECK(halos->count == count);

	for (n = 0; n < halos->count; n++)
		CHECK(!halos->sources[n].lenses);

	settings.halos = 0;
	CHECK(mh_race_draw_frame(loop));
	CHECK(halos->count == 0);
	CHECK(mh_renderer_tga_capture(loop->renderer, &without_halos));
	CHECK(with_halos.size == without_halos.size);
	halo_picture_changes += memcmp(with_halos.data, without_halos.data,
					with_halos.size) != 0;
	free(with_halos.data);
	free(without_halos.data);
	settings.halos = settings.lens_flares = 1;

	TRY(mh_race_scene_world_view_profile(MH_RENDER_SOFTWARE, &field,
		&context.scene.assets.lighting_world, loop->width, loop->height,
		&view, &error));
	saved = field.unavailable_mask;
	field.unavailable_mask = (1U << field.count) - 1;
	TRY(mh_race_scene_update_software_halos(&context.scene, &field,
		loop->models, &view, &settings, &error));

	for (n = 0; n < halos->count; n++)
		CHECK(halos->sources[n].model == NULL);

	field.unavailable_mask = saved;
	saved = field.active_mask;
	field.active_mask = 0;
	TRY(mh_race_scene_update_software_halos(&context.scene, &field,
		loop->models, &view, &settings, &error));

	for (n = 0; n < halos->count; n++)
		CHECK(halos->sources[n].model == NULL);

	field.active_mask = saved;
	CHECK(mh_race_draw_frame(loop));
	CHECK(halos->count == count);

	return 1;
}

static int
unavailable_frame(struct mh_race_loop *loop)
{
	struct mh_model_lighting lighting;
	struct mh_file baseline, outline;
	struct mh_error error;
	unsigned int saved, i;

	/* A leading cockpit view can contain no visible cars at all. */
	TRY(mh_field_camera_mode(&field, MH_CAMERA_CHASE, &error));

	for (i = 0; i < 120; i++)
		TRY(mh_drive_camera_update(loop->viewed, 1.0f / 30, &error));

	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &baseline));
	saved = field.unavailable_mask;
	field.unavailable_mask = (1U << field.count) - 1;

	for (i = 0; i < loop->mesh_count; i++) {
		TRY(mh_race_scene_software_lighting(&context.scene, &field, i,
			&settings, &lighting, &error));
		CHECK(lighting.mode ==
			(i == 0 ? MH_LIGHT_OBJECT : MH_LIGHT_UNAVAILABLE));
	}

	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &outline));
	CHECK(outline.size == baseline.size &&
		memcmp(outline.data, baseline.data, outline.size) != 0);
	free(outline.data);
	field.unavailable_mask = saved;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &outline));
	CHECK(outline.size == baseline.size &&
		memcmp(outline.data, baseline.data, outline.size) == 0);
	free(outline.data);
	free(baseline.data);

	return 1;
}

static int
nameplates_frame(struct mh_race_loop *loop)
{
	struct mh_renderer_name sources[MH_NAME_LABELS_MAX];
	struct mh_file none, flat, transparent;
	struct mh_error error;
	size_t count, i;
	unsigned int saved_mode, saved_unavailable, all;

	all = (1U << field.count) - 1;
	TRY(mh_race_scene_software_names(&context.scene, &field, loop->models,
		all, 1, sources, &count, &error));
	CHECK(count == field.count - 1);

	/* Hold the settled pose but use the track-camera roster policy. */
	TRY(mh_field_camera_mode(&field, MH_CAMERA_TRACK, &error));
	saved_mode = loop->names_mode;
	saved_unavailable = field.unavailable_mask;
	field.unavailable_mask = all;
	TRY(mh_race_scene_software_names(&context.scene, &field, loop->models,
		all, 1, sources, &count, &error));
	CHECK(count == field.count);

	for (i = 0; i < count; i++) {
		CHECK(sources[i].part == 2 + i * (MH_CAR_MESHES + 1));
		CHECK(sources[i].model ==
			loop->models + (1 + i * MH_CAR_MESHES) * 16);
		CHECK(sources[i].image ==
			&context.scene.assets.software.names.images[i]);
		CHECK(sources[i].image->height == 20);
	}

	field.unavailable_mask = saved_unavailable;
	loop->names_mode = MH_AV_NAMES_NONE;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &none));
	loop->names_mode = MH_AV_NAMES_FLAT;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &flat));
	loop->names_mode = MH_AV_NAMES_TRANSPARENT;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &transparent));
	CHECK(none.size == flat.size && flat.size == transparent.size);
	CHECK(memcmp(none.data, flat.data, none.size) != 0);
	CHECK(memcmp(flat.data, transparent.data, flat.size) != 0);
	free(flat.data);
	free(transparent.data);
	loop->names_mode = MH_AV_NAMES_NONE;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &flat));
	CHECK(flat.size == none.size &&
		memcmp(flat.data, none.data, flat.size) == 0);
	free(flat.data);
	free(none.data);
	loop->names_mode = saved_mode;
	TRY(mh_field_camera_mode(&field, MH_CAMERA_CHASE, &error));
	CHECK(mh_race_draw_frame(loop));

	return 1;
}

static int
software_only(struct mh_race_loop *loop)
{
	CHECK(!loop->tron && !loop->retro);
	CHECK(context.scene.assets.retro == NULL);
	CHECK(context.scene.visuals.sky_mesh.vertex_count == 0);
	CHECK(context.scene.visuals.tron_time == 0);
	CHECK(loop->water_frame.phase == 0);

	return 1;
}

/* Weather and brightness stores. */
static int
weather_frames(struct mh_race_loop *loop)
{
	static const struct weather_case cases[] = {
		{MH_CHEAT_UNDERWATER, 0, 0, {.5, 1, 1.2000000476837158},
			{0, 100, 64}, .9000000357627869f},
		{MH_CHEAT_THUNDER, 0, 12345,
			{.80000001192092896, .40000000596046448,
				.40000000596046448},
			{0, 0, 0}, .53333336114883423f},
		{MH_CHEAT_THUNDER, 579577475UL, 1317273600UL, {3, 3, 3},
			{255, 255, 255}, 3}};

	struct mh_race_visuals *visuals;
	struct mh_file baseline, picture;
	struct mh_brightness brightness;
	struct mh_error error;
	unsigned long random, ticks;
	size_t n;
	unsigned int i;
	int dimmed;

	visuals = &context.scene.visuals;
	random = visuals->particle_system.random;
	dimmed = visuals->pause_dim;
	ticks = field.cars[0].ticks;
	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &baseline));
	brightness = visuals->brightness;
	TRY(mh_race_scene_pause(&context.scene, 1, &settings, &error));

	for (n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
		field.cars[0].cheats = cases[n].flags;
		visuals->particle_system.random = cases[n].seed;
		CHECK(mh_race_draw_frame(loop));
		CHECK(software_only(loop));
		CHECK(loop->weather != NULL);
		CHECK(visuals->particle_system.random == cases[n].next);
		CHECK(field.cars[0].ticks == ticks);
		CHECK(visuals->brightness.particle == cases[n].particle);

		for (i = 0; i < 3; i++) {
			CHECK(visuals->brightness.world[i] == cases[n].rgb[i]);
			CHECK(visuals->brightness.object[i] == cases[n].rgb[i]);
			CHECK(loop->weather->fog[i] == cases[n].fog[i]);
		}

		CHECK(mh_renderer_tga_capture(loop->renderer, &picture));
		CHECK(picture.size == baseline.size &&
			memcmp(picture.data, baseline.data, picture.size) != 0);
		free(picture.data);

		/*
		 * Results do not render weather or consume its random sample.
		 */
		loop->screen.visible = 1;
		CHECK(mh_race_draw_frame(loop));
		CHECK(visuals->particle_system.random == cases[n].next);
		loop->screen.visible = 0;
	}

	field.cars[0].cheats = 0;
	TRY(mh_race_scene_pause(&context.scene, dimmed, &settings, &error));
	CHECK(mh_race_draw_frame(loop));
	CHECK(loop->weather == NULL);
	CHECK(memcmp(brightness.world, visuals->brightness.world,
		sizeof(brightness.world)) == 0);
	CHECK(memcmp(brightness.object, visuals->brightness.object,
		sizeof(brightness.object)) == 0);
	CHECK(brightness.particle == visuals->brightness.particle);
	CHECK(mh_renderer_tga_capture(loop->renderer, &picture));
	CHECK(picture.size == baseline.size &&
		memcmp(picture.data, baseline.data, picture.size) == 0);
	free(picture.data);
	free(baseline.data);
	visuals->particle_system.random = random;

	return 1;
}

static int
run_frames(struct mh_race_loop *loop, unsigned int track, const char *capture)
{
	unsigned int i;
	unsigned long ticks;

	/* Original GO is after twelve seconds; include eight moving seconds. */
	for (i = 0; i < 600; i++) {
		if (mh_race_loop_step(loop) != MH_APP_RUNNING) {
			fprintf(stderr, "track %u failed at frame %u\n", track,
				i);
			return 0;
		}

		count_effects();
	}

	CHECK(advance_calls == 600);
	CHECK(field.cars[0].frame.time.go_ms != 0);
	CHECK(field.cars[0].vehicle.speed > 0);

	if (capture != NULL && track == 1)
		CHECK(capture_frame(loop->renderer, capture));

	/*
	 * Exercise held pause and resident results without rebuilding assets.
	 */
	ticks = field.cars[0].ticks;
	loop->paused = 1;
	CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);
	CHECK(field.cars[0].ticks == ticks);
	CHECK(halo_settings(loop));
	CHECK(unavailable_frame(loop));
	CHECK(nameplates_frame(loop));
	CHECK(weather_frames(loop));
	loop->screen.visible = 1;
	CHECK(mh_race_draw_frame(loop));
	loop->screen.visible = 0;
	loop->paused = 0;
	CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);
	CHECK(field.cars[0].ticks > ticks);

	return 1;
}

static int
ordinary_hidden_frame(struct mh_race_loop *loop, unsigned int mode)
{
	struct mh_file hidden, ordinary;
	unsigned int car;

	CHECK(mh_renderer_tga_capture(loop->renderer, &hidden));

	for (car = 0; car < field.count; car++)
		field.cars[car].cheats = 0;

	CHECK(mh_race_draw_frame(loop));
	CHECK(mh_renderer_tga_capture(loop->renderer, &ordinary));
	CHECK(hidden.size == ordinary.size &&
		memcmp(hidden.data, ordinary.data, hidden.size) == 0);
	free(hidden.data);
	free(ordinary.data);

	for (car = 0; car < field.count; car++)
		field.cars[car].cheats = mode;

	return 1;
}

static int
hidden_frames(struct mh_race_loop *loop, unsigned int mode)
{
	const struct mh_race_visuals *visuals;
	const struct mh_particle_pool *pool;
	unsigned int frame, car, wheel, smoke_frames, i;
	unsigned long ticks, random;

	visuals = &context.scene.visuals;
	smoke_frames = 0;
	CHECK(software_only(loop));

	for (frame = 0; frame < 600; frame++) {
		CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);
		CHECK(software_only(loop));

		for (car = 0; car < field.count; car++) {
			CHECK(field.cars[car].cheats == mode);

			if (mode != MH_CHEAT_UNDERWATER)
				continue;

			for (wheel = 0; wheel < 4; wheel++)
				CHECK(visuals->skids[car].wheels[wheel].count ==
					0);

			for (wheel = 0; wheel < 2; wheel++) {
				pool = &visuals->particles[car].pools[wheel];
				smoke_frames += pool->count != 0;

				for (i = 0; i < MH_PARTICLE_CAPACITY; i++)
					if (pool->particles[i].active)
						CHECK(pool->particles[i]
								.animation ==
							MH_PARTICLE_SMOKE);
			}
		}
	}

	CHECK(field.cars[0].frame.time.go_ms != 0);
	CHECK(field.cars[0].vehicle.speed > 0);

	if (mode == MH_CHEAT_UNDERWATER)
		CHECK(smoke_frames != 0);

	if (mode == MH_CHEAT_SUPERCARS || mode == MH_CHEAT_IGNITION)
		CHECK(!mh_drive_camera_in_car(loop->viewed));

	ticks = field.cars[0].ticks;
	loop->paused = 1;
	CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);
	CHECK(field.cars[0].ticks == ticks);
	CHECK(software_only(loop));

	if (mode == MH_CHEAT_TRON || mode == MH_CHEAT_RETRO)
		CHECK(ordinary_hidden_frame(loop, mode));

	random = visuals->particle_system.random;
	loop->screen.visible = 1;
	CHECK(mh_race_draw_frame(loop));
	CHECK(visuals->particle_system.random == random);
	loop->screen.visible = 0;
	loop->paused = 0;
	CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);
	CHECK(field.cars[0].ticks > ticks);

	return 1;
}

static int
finish_frames(struct mh_race_loop *loop, unsigned int track)
{
	struct mh_standings final;
	struct mh_file picture, held;
	unsigned int frame;
	unsigned long ticks, random;

	/*
	 * Full physical race, rendering every supplied 30 Hz simulation frame.
	 */
	for (frame = 0; frame < 18000 && !loop->screen.prepared; frame++) {
		CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);

		if (frame % 1000 == 0) {
			printf("Software finish: track %u, frame %u\n", track,
				frame);
			fflush(stdout);
		}
	}

	CHECK(field.cars[0].race.finished);
	CHECK(loop->screen.prepared && loop->screen.visible);
	CHECK(loop->screen.final.count == 8);
	CHECK(loop->screen.final.cars[0].finished);
	CHECK(loop->screen.final.cars[0].position >= 1 &&
		loop->screen.final.cars[0].position <= 8);
	CHECK(!loop->screen.final.cars[0].estimated);
	CHECK(mh_renderer_tga_capture(loop->renderer, &picture));
	final = loop->screen.final;
	ticks = field.cars[0].ticks;
	random = context.scene.visuals.particle_system.random;

	for (frame = 0; frame < 3; frame++)
		CHECK(mh_race_loop_step(loop) == MH_APP_RUNNING);

	CHECK(field.cars[0].ticks == ticks);
	CHECK(context.scene.visuals.particle_system.random == random);
	CHECK(memcmp(&final, &loop->screen.final, sizeof(final)) == 0);
	CHECK(mh_renderer_tga_capture(loop->renderer, &held));
	CHECK(picture.size == held.size &&
		memcmp(picture.data, held.data, picture.size) == 0);
	free(picture.data);
	free(held.data);
	printf("Software finish: track %u, local position %u, held results passed\n",
		track, final.cars[0].position);
	fflush(stdout);

	return 1;
}

static int
run_race(const char *root, unsigned int track, const char *capture,
	unsigned int mode, int finish)
{
	struct mh_race_loop_options options;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct mh_race_loop *loop;
	struct mh_error error;
	int success;

	memset(&options, 0, sizeof(options));
	options.renderers = mh_diagnostic_renderers();
	options.windows = mh_sdl_window_system();
	options.inputs = mh_sdl_input_provider();
	options.audio_provider = mh_sdl_audio_provider();
	options.retro_source = mh_retro_fallback_source();
	options.audio_decoder = mh_audio_fallback_decoder();
	options.audio_trace = 0;
	options.timer = mh_game_clock_timer();
	memset(&session, 0, sizeof(session));
	memset(&audio, 0, sizeof(audio));
	session.advance = advance;
	session.pause_with_ui = 1;
	options.headless = 1;
	options.root = root;
	options.session = &session;
	options.audio_options = &audio;
	advance_calls = 0;
	TRY(mh_race_loop_open(&context, &field, &options, &loop, &error));

	if (finish)
		success = finish_frames(loop, track);
	else
		success = mode == 0 ? run_frames(loop, track, capture)
				: hidden_frames(loop, mode);

	if (!mh_race_loop_close(loop))
		success = 0;

	CHECK(success);

	if (!finish)
		printf("Software live race: track %u, mode %u, eight cars, pause/results/resume passed\n",
			track, mode);

	return 1;
}

static int
hidden_modes(const char *root)
{
	static const unsigned int modes[] = {MH_CHEAT_UNDERWATER,
		MH_CHEAT_THUNDER, MH_CHEAT_SUPERCARS, MH_CHEAT_IGNITION,
		MH_CHEAT_TRON, MH_CHEAT_RETRO};

	struct mh_error error;
	size_t i;
	int success;

	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
		success = load_race(root, 1);

		if (success) {
			TRY(mh_field_set_cheats(&field, modes[i], &error));
			success = run_race(root, 1, NULL, modes[i], 0);
		}

		mh_race_context_free(&context);
		CHECK(success);
	}

	return 1;
}

int
main(int argc, char *argv[])
{
	unsigned int track;
	int success, finish;

	if (argc < 2 || argc > 3)
		return 2;

	if (argc == 3 && strcmp(argv[2], "--hidden") == 0)
		return hidden_modes(argv[1]) ? 0 : 1;

	finish = argc == 3 && strcmp(argv[2], "--finish") == 0;

	for (track = 1; track <= 8; track++) {
		success = load_race(argv[1], track);

		if (success)
			success = run_race(argv[1], track,
				argc == 3 && !finish ? argv[2] : NULL, 0,
				finish);

		mh_race_context_free(&context);

		if (!success)
			return 1;
	}

	if (finish)
		return 0;

	printf("Effect-bearing frames: shadows %u, skid histories %u, particle pools %u\n",
		shadow_frames, skid_frames, particle_frames);

	printf("Halo-bearing frames: world %u, car %u, scenery %u; %u tracks change pixels with halos toggled\n",
		world_halo_frames, car_halo_frames, scenery_halo_frames,
		halo_picture_changes);

	if (world_halo_frames == 0 || car_halo_frames == 0 ||
		scenery_halo_frames == 0 || halo_picture_changes == 0)
		return 1;

	if (shadow_frames == 0 || skid_frames == 0 || particle_frames == 0)
		return 1;

	return 0;
}

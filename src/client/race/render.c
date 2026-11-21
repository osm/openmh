#include <math.h>
#include <string.h>

#include "client/race/internal.h"
#include "engine/core/error.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/scene_matrices.h"

#define RESULTS_FOOTER_SIZE 128

static float race_water_phase;
static struct mh_retro_draw race_retro_draw;

static int
prepare_weather(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;
	visuals = &scene->visuals;

	/*
	 * Consume the sampled frame time on every rendered frame, including
	 * menu opening/held/closing. Inspection and capture suspension still
	 * retain their presentation state.
	 */
	loop->effect_dt =
		loop->frame_updated ? loop->drive->frame.time.elapsed : 0;

	if (loop->tick_clock.rate &&
		loop->frame_request.action == MH_FRAME_RUN &&
		loop->capture == NULL)
		loop->effect_dt =
			(float)(loop->wall_elapsed > 1 ? 1
						: loop->wall_elapsed);

	loop->tron_before = visuals->tron_time;
	loop->retro_before = visuals->retro_state;
	loop->water_before = race_water_phase;
	loop->weather_random = visuals->particle_system.random;

	if (!visuals->brightness_ready)
		loop->brightness_changed = 1;

	loop->previous_brightness = visuals->brightness;
	status = mh_race_scene_weather(
		scene, loop->field, loop->context->settings, &loop->error);
	loop->weather = visuals->weather;

	if (memcmp(loop->previous_brightness.world, visuals->brightness.world,
		sizeof(loop->previous_brightness.world)) != 0)
		loop->brightness_changed = 1;

	if (status == MH_ERROR_NONE && assets->profile != MH_RENDER_SOFTWARE &&
		(loop->weather != NULL || assets->track.has_cue_color)) {
		status = mh_fog_configure(loop->weather != NULL
				? loop->weather->fog
				: assets->track.cue_color,
			mh_drive_camera_far(loop->viewed), &loop->fog,
			&loop->error);

		if (status == MH_ERROR_NONE &&
			!mh_renderer_set_fog(loop->renderer, &loop->fog))
			status =
				mh_renderer_error(loop->renderer, &loop->error);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Weather: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	return 1;
}

static int
prepare_scene_view(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	loop->particle_camera.brightness = visuals->brightness.particle;
	mh_field_matrices(loop->field, (double)loop->width / loop->height,
		loop->models, loop->matrix, loop->view);

	if (loop->glenz_loaded) {
		status = mh_race_scene_glenz_scroll(scene, loop->field,
			loop->models,
			loop->context->settings == NULL ||
				loop->context->settings->shadow,
			&loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Glenz scroll: %s\n",
				loop->error.message);
			loop->success = loop->running = 0;
			return 0;
		}
	}

	status = mh_race_scene_details(scene, loop->field, loop->models,
		loop->width, mh_renderer_profile(loop->renderer), &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_visibility_profile(
			mh_renderer_profile(loop->renderer), scene, loop->field,
			loop->models, loop->width, loop->height,
			&loop->visible_roots, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Car detail: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	if (scene->assets.profile != MH_RENDER_SOFTWARE &&
		!mh_renderer_depth_read(loop->renderer, visuals->lamps.visible,
			visuals->lamps.count)) {
		mh_renderer_error(loop->renderer, &loop->error);
		fprintf(stderr, "Lamp visibility: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	return 1;
}

static int
prepare_sky_halos(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;
	unsigned int i;

	scene = &loop->context->scene;
	assets = &scene->assets;
	visuals = &scene->visuals;

	if (loop->weather != NULL) {
		memset(&loop->weather_camera, 0, sizeof(loop->weather_camera));
		loop->weather_camera.width = loop->width;
		loop->weather_camera.height = loop->height;
		loop->weather_camera.focal =
			(float)sqrt((double)loop->matrix[0] * loop->matrix[0] +
				(double)loop->matrix[4] * loop->matrix[4] +
				(double)loop->matrix[8] * loop->matrix[8]);

		for (i = 0; i < 3; i++) {
			loop->weather_camera.forward[i] =
				-loop->view[i * 4 + 2];
			loop->weather_camera.position[i] =
				(float)loop->viewed->eye[i];
		}

		loop->weather_camera.underwater =
			(loop->drive->cheats & MH_CHEAT_THUNDER) == 0;
		loop->weather_camera.top = loop->weather->top;
		loop->weather_camera.bottom = loop->weather->bottom;
		status = mh_sky_mesh_weather(&visuals->sky_mesh,
			&loop->weather_camera, &loop->error);

		if (status == MH_ERROR_NONE &&
			loop->weather_camera.underwater) {
			status = mh_water_frame(&race_water_phase, loop->width,
				loop->height, &loop->water_frame, &loop->error);

			if (status == MH_ERROR_NONE &&
				!mh_renderer_set_water(
					loop->renderer, &loop->water_frame))
				status = mh_renderer_error(
					loop->renderer, &loop->error);
		}
	} else if (loop->viewed->cheats &
		(MH_CHEAT_IGNITION | MH_CHEAT_SUPERCARS))
		status = mh_sky_mesh_black(&visuals->sky_mesh, &loop->error);
	else if (loop->context->settings != NULL &&
		!loop->context->settings->sky_image)
		status = mh_sky_mesh_plain(&visuals->sky_mesh, &loop->sky_state,
			&assets->track, loop->matrix, loop->view, loop->width,
			loop->height, &loop->error);
	else
		status = mh_sky_mesh_update(&visuals->sky_mesh,
			&loop->sky_state, &assets->track, loop->matrix,
			loop->view, loop->width, loop->height, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_update_lamps(scene, loop->field,
			loop->models, loop->matrix, loop->view, loop->width,
			loop->height, loop->context->settings, &loop->error);

	if (status != MH_ERROR_NONE ||
		!mh_renderer_mesh_sky(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_SKY_MESH) ||
		!mh_renderer_mesh_halos(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_LAMP_MESH)) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Sky/halos: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static int
prepare_nameplates(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	enum mh_error_code status;

	scene = &loop->context->scene;

	status = mh_race_scene_names(scene, loop->field, loop->models,
		loop->view, loop->width, loop->height, loop->names_mode != 0,
		&loop->error);

	if (status != MH_ERROR_NONE ||
		!mh_renderer_mesh_halos(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_NAME_MESH)) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Car names: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static int
light_scenery(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;
	visuals = &scene->visuals;
	status = loop->error.code;

	if (scene->light_mesh != NULL) {
		status = scene->light_mesh(scene, loop->viewed, loop->renderer,
			0, !loop->tron, &loop->error);

		if (status != MH_ERROR_NONE)
			return loop->success = loop->running = 0;
	}

	if (assets->meshes[0].scenery != NULL &&
		(scene->light_mesh == NULL || loop->tron)) {
		status = (assets->profile == MH_RENDER_DIRECT3D ||
					assets->profile == MH_RENDER_POWERVR
				? mh_mesh_material_light_scene
				: mh_mesh_light_scene)(&assets->meshes[0],
			&assets->lighting_world, assets->track.object_ambient,
			visuals->brightness.object,
			assets->track.light_intensity, loop->viewed->eye,
			visuals->prepared_lights,
			assets->lighting_world.light_count, &loop->error);
	}

	if (status == MH_ERROR_NONE &&
		(assets->meshes[0].scenery != NULL ||
			loop->brightness_changed || loop->was_results) &&
		!mh_renderer_mesh_lit(
			loop->renderer, 0, assets->meshes[0].vertices))
		status = mh_renderer_error(loop->renderer, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Scenery lighting: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	return 1;
}

static int
prepare_skid_mesh(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;
	status = loop->error.code;

	if (loop->context->settings == NULL ||
		loop->context->settings->skid_marks)
		status = mh_skid_mesh_update(&visuals->skid_mesh,
			visuals->skids, MH_RACE_MAX_CARS, loop->matrix,
			loop->viewed->eye, &loop->error);
	else
		memset(visuals->skid_mesh.vertices, 0,
			visuals->skid_mesh.vertex_count *
				sizeof(*visuals->skid_mesh.vertices));

	if (status != MH_ERROR_NONE ||
		!mh_renderer_mesh_skids(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_SKID_MESH)) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Tyre mark mesh: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static int
prepare_shadow_mesh(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;
	status = loop->error.code;

	if (loop->context->settings == NULL || loop->context->settings->shadow)
		status = mh_shadow_mesh_update(&visuals->shadow_mesh,
			loop->field, loop->models, loop->matrix,
			loop->viewed->eye, &loop->error);
	else
		memset(visuals->shadow_mesh.vertices, 0,
			visuals->shadow_mesh.vertex_count *
				sizeof(*visuals->shadow_mesh.vertices));

	if (status != MH_ERROR_NONE ||
		!mh_renderer_mesh_shadows(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_SHADOW_MESH)) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Shadows: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static int
prepare_particle_mesh(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	loop->particle_camera.far_depth = mh_drive_camera_far(loop->viewed);
	memcpy(loop->particle_camera.matrix, loop->matrix,
		sizeof(loop->matrix));
	status = mh_particle_mesh_update(&visuals->particle_mesh,
		visuals->particles, MH_RACE_MAX_CARS, &loop->particle_camera,
		&loop->error);

	if (status != MH_ERROR_NONE ||
		!mh_renderer_mesh_particles(loop->renderer,
			loop->mesh_count + MH_SCENE_GPU_PARTICLE_MESH)) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Particle mesh: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static int
car_mesh_visible(const struct mh_race_loop *loop, unsigned int mesh)
{
	unsigned int entry;

	entry = (mesh - 1) / MH_CAR_MESHES;

	return (loop->visible_roots & (1U << entry)) != 0 &&
		mh_race_scene_part_visible(
			&loop->context->scene, loop->field, mesh);
}

static enum mh_error_code
set_car_visibility(struct mh_race_loop *loop, struct mh_error *error_out)
{
	unsigned int i;

	for (i = 1; i < loop->mesh_count; i++) {
		if (!mh_renderer_mesh_visible(
			loop->renderer, i, car_mesh_visible(loop, i))) {
			return mh_renderer_error(loop->renderer, error_out);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
configure_car_glenz(struct mh_race_loop *loop, unsigned int mesh,
	int glenz_enabled, struct mh_error *error_out)
{
	struct mh_renderer_glenz glenz;
	enum mh_error_code status;

	if (!glenz_enabled) {
		status = mh_renderer_mesh_glenz(
			loop->renderer, mesh, NULL, error_out);
	} else {
		status = mh_race_scene_glenz_material(&loop->context->scene,
			mesh, loop->models, loop->matrix, loop->view,
			loop->viewed->eye, loop->width, loop->height, &glenz,
			error_out);

		if (status == MH_ERROR_NONE)
			status = mh_renderer_mesh_glenz(
				loop->renderer, mesh, &glenz, error_out);
	}

	return status;
}

static int
prepare_car_meshes(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;
	unsigned int i;
	unsigned int entry;
	int glenz_enabled;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	glenz_enabled = loop->glenz_loaded && loop->reflections &&
		(loop->debug_glenz ||
			loop->menu_settings.shading == MH_AV_SHADING_GLENZ);

	status = set_car_visibility(loop, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Car visibility: %s\n", loop->error.message);
		loop->success = 0;
	}

	if (!loop->success) {
		loop->running = 0;
		return 0;
	}

	for (i = 1; i < loop->mesh_count; i++) {
		entry = (i - 1) / MH_CAR_MESHES;

		/* Hidden parts are shaded and uploaded when visible again. */
		if (!car_mesh_visible(loop, i))
			continue;

		if (scene->light_mesh != NULL) {
			status = scene->light_mesh(scene, loop->viewed,
				loop->renderer, i,
				!loop->tron &&
					!(loop->field->unavailable_mask &
						(1U << entry)),
				&loop->error);

			if (status != MH_ERROR_NONE) {
				loop->success = loop->running = 0;
				return 0;
			}
		}

		if (loop->field->unavailable_mask & (1U << entry)) {
			status = mh_race_scene_shade_unavailable(scene,
				loop->field, i, loop->models, loop->view,
				&loop->error);

			if (status == MH_ERROR_NONE)
				loop->success = mh_renderer_mesh_unavailable(
					loop->renderer, i,
					visuals->shaded_vertices[i]);
		} else if (scene->light_mesh != NULL && !loop->tron) {
			status = MH_ERROR_NONE;
		} else {
			status = mh_race_scene_shade_car(scene, loop->field, i,
				loop->models, loop->view, glenz_enabled,
				loop->reflections, &loop->error);

			if (status == MH_ERROR_NONE)
				loop->success =
					mh_renderer_mesh_lit(loop->renderer, i,
						visuals->shaded_vertices[i]);
		}

		if (loop->glenz_loaded && status == MH_ERROR_NONE &&
			loop->success &&
			!(loop->field->unavailable_mask & (1U << entry))) {
			status = configure_car_glenz(
				loop, i, glenz_enabled, &loop->error);
		}

		if (status != MH_ERROR_NONE || !loop->success) {
			if (status == MH_ERROR_NONE)
				mh_renderer_error(loop->renderer, &loop->error);

			fprintf(stderr, "Car lighting: %s\n",
				loop->error.message);
			loop->success = 0;
			break;
		}
	}

	if (!loop->success) {
		loop->running = 0;
		return 0;
	}

	return 1;
}

static enum mh_error_code
prepare_projection(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_camera_lens_setup(
		(loop->viewed->camera_goal ||
			loop->viewed->camera_mode == MH_CAMERA_TRACK)
			? loop->viewed->camera_focal
			: MH_CAMERA_RACE_FOCAL,
		MH_CAMERA_RACE_NEAR, mh_drive_camera_far(loop->viewed),
		(float)loop->width / loop->height, &loop->scene_lens,
		error_out);

	if (status == MH_ERROR_NONE)
		status = mh_projection_setup(loop->width, loop->height,
			(float)loop->width / loop->height,
			loop->scene_lens.focal, loop->scene_lens.near_depth,
			loop->scene_lens.far_depth, &loop->scene_projection,
			error_out);

	return status;
}

static int
draw_world(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;
	visuals = &scene->visuals;
	status = MH_ERROR_NONE;

	if (loop->tron || assets->profile == MH_RENDER_DIRECT3D ||
		assets->profile == MH_RENDER_POWERVR) {
		status = prepare_projection(loop, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Scene projection: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	if (loop->tron) {
		loop->success = mh_renderer_tron_scene_ui(loop->renderer,
			&loop->scene_projection, loop->view, loop->models,
			loop->reflections, &loop->ui);

		if (loop->success)
			visuals->tron_time = loop->tron_intro.next_time;
	} else if (loop->retro) {
		status = mh_retro_draw(assets->retro, &loop->context->hud,
			loop->width, loop->height, loop->effect_dt,
			&visuals->retro_state, &race_retro_draw, &loop->error);

		if (status == MH_ERROR_NONE &&
			(assets->profile == MH_RENDER_DIRECT3D ||
				assets->profile == MH_RENDER_POWERVR)) {
			status = mh_renderer_scene_frame_lens(loop->renderer,
				&loop->scene_projection, loop->view,
				loop->models, loop->reflections, &loop->ui,
				&race_retro_draw, NULL, &loop->error);
			loop->success = status == MH_ERROR_NONE;
		} else if (status == MH_ERROR_NONE)
			loop->success = mh_renderer_scene_frame_retro(
				loop->renderer, loop->matrix, loop->view,
				loop->models, loop->reflections, &loop->ui,
				&race_retro_draw);
		else {
			fprintf(stderr, "Retro frame: %s\n",
				loop->error.message);
			loop->success = 0;
		}

		if (loop->success) {
			visuals->retro_state = race_retro_draw.next;

			if (loop->capture == NULL)
				loop->history->retro = race_retro_draw.next;
		}
	} else if (assets->profile == MH_RENDER_DIRECT3D ||
		assets->profile == MH_RENDER_POWERVR) {
		status = mh_renderer_scene_frame_lens(loop->renderer,
			&loop->scene_projection, loop->view, loop->models,
			loop->reflections, &loop->ui, NULL, NULL, &loop->error);
		loop->success = status == MH_ERROR_NONE;
	} else
		loop->success = mh_renderer_scene_frame_ui(loop->renderer,
			loop->matrix, loop->view, loop->models,
			loop->reflections, &loop->ui);

	if (!loop->success) {
		if (status == MH_ERROR_NONE)
			mh_renderer_error(loop->renderer, &loop->error);

		fprintf(stderr, "Frame: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;

		return 0;
	}

	return 1;
}

static void
trace_hidden_effects(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	if (loop->debug_hidden &&
		(loop->tron || loop->retro || loop->weather != NULL)) {
		fprintf(stderr,
			"hidden-frame %u %d %d %d %.9g %.9g %.9g %.9g "
			"%u %u %.9g %.9g %.9g %.9g %.9g %.9g %lu %lu %.9g %.9g\n",
			loop->drive->cheats, loop->entry_menu,
			loop->menu.visible, loop->frame_updated,
			loop->drive->frame.time.elapsed, loop->effect_dt,
			loop->tron_before, visuals->tron_time,
			loop->retro_before.line, visuals->retro_state.line,
			loop->retro_before.scroll, visuals->retro_state.scroll,
			loop->retro_before.wave, visuals->retro_state.wave,
			loop->retro_before.time, visuals->retro_state.time,
			loop->weather_random, visuals->particle_system.random,
			loop->water_before, race_water_phase);
	}
}

static int
update_lamp_history(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	/* PowerVR lamps do not use depth-query visibility history. */
	if (scene->assets.profile == MH_RENDER_POWERVR)
		return 1;

	if (!loop->tron &&
		!mh_renderer_depth_queue(loop->renderer, visuals->lamps.probes,
			visuals->lamps.count)) {
		mh_renderer_error(loop->renderer, &loop->error);
		fprintf(stderr, "Lamp depth queries: %s\n",
			loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	/*
	 * Headless captures render once after advancing the
	 * simulation. Render one additional frame to populate
	 * lamp visibility history.
	 */
	if (loop->capture != NULL && !loop->tron) {
		if (!mh_renderer_depth_read(loop->renderer,
			visuals->lamps.visible, visuals->lamps.count)) {
			mh_renderer_error(loop->renderer, &loop->error);
			fprintf(stderr, "Capture lamp visibility: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		status = mh_race_scene_update_lamps(scene, loop->field,
			loop->models, loop->matrix, loop->view, loop->width,
			loop->height, loop->context->settings, &loop->error);

		if (status != MH_ERROR_NONE ||
			!mh_renderer_mesh_halos(loop->renderer,
				loop->mesh_count + MH_SCENE_GPU_LAMP_MESH) ||
			!mh_renderer_scene_frame_retro(loop->renderer,
				loop->matrix, loop->view, loop->models,
				loop->reflections, &loop->ui,
				loop->retro ? &race_retro_draw : NULL)) {
			if (status == MH_ERROR_NONE)
				mh_renderer_error(loop->renderer, &loop->error);

			fprintf(stderr, "Capture lamps: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;

			return 0;
		}
	}

	return 1;
}

int
mh_race_animate_scene(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;

	if (!loop->screen.visible && assets->meshes[0].scenery != NULL &&
		(loop->tick_clock.rate || loop->frame_updated ||
			loop->capture != NULL || loop->was_results)) {
		status = mh_race_scene_animate_objects(
			scene, loop->field, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Animation: %s\n", loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	return 1;
}

static void
format_results_footer(
	const struct mh_race_loop *loop, char footer[RESULTS_FOOTER_SIZE])
{
	if (loop->result_handler != NULL && loop->screen.prepared)
		strcpy(footer,
			loop->save_failed ? "Save failed  |  Enter/Esc: Retry"
					: "Enter/Esc: Continue");

	else if (loop->continuous)
		strcpy(footer, "Live race  |  Enter/Esc: Back");
	else
		sprintf(footer, "%s  |  Enter/Esc: Back",
			loop->paused			? "Paused"
				: loop->screen.prepared ? "Race results"
				: loop->field->standings.complete
				? "Race complete"
				: loop->drive->race.finished
				? "Waiting for finishers"
				: "Race paused");
}

int
mh_race_draw_frame(struct mh_race_loop *loop)
{
	char footer[RESULTS_FOOTER_SIZE];

	loop->messages.sound = 0;

	if (loop->screen.visible) {
		format_results_footer(loop, footer);

		if (!mh_results_draw(&loop->context->results,
			mh_race_context_atlas(loop->context),
			loop->screen.prepared ? &loop->screen.final
						: &loop->field->standings,
			loop->width, loop->height, footer, &loop->ui) ||
			!mh_race_frame_rate_draw(loop)) {
			fprintf(stderr, "Results layout failed\n");
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		if (!mh_renderer_ui_frame(loop->renderer, &loop->ui)) {
			mh_renderer_error(loop->renderer, &loop->error);
			fprintf(stderr, "Results frame: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	} else {
		if (!prepare_weather(loop))
			return 0;

		if (!prepare_scene_view(loop))
			return 0;

		if (!mh_race_prepare_finish(loop))
			return 0;

		if (MH_RENDER_BUILD_SOFTWARE &&
			loop->context->scene.assets.profile ==
				MH_RENDER_SOFTWARE)
			return mh_race_draw_software_scene(loop);

		if (!prepare_sky_halos(loop))
			return 0;

		if (!prepare_nameplates(loop))
			return 0;

		if (!light_scenery(loop))
			return 0;

		if (!prepare_skid_mesh(loop))
			return 0;

		if (!prepare_shadow_mesh(loop))
			return 0;

		if (!prepare_particle_mesh(loop))
			return 0;

		if (!mh_race_prepare_hud(loop))
			return 0;

		if (!mh_race_build_overlay(loop))
			return 0;

		if (!prepare_car_meshes(loop))
			return 0;

		if (!draw_world(loop))
			return 0;

		trace_hidden_effects(loop);

		if (!update_lamp_history(loop))
			return 0;
	}

	return 1;
}

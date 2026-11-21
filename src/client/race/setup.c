#include <stdlib.h>
#include <string.h>

#include "client/loading/view.h"
#include "client/race/internal.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/render/software_scene.h"
#include "engine/window/title.h"
#include "interfaces/render/backend.h"

#define DEFAULT_CHAT_BINDING 0x214UL

static int
validate_audio_options(struct mh_race_loop *loop)
{
	if (loop->audio_options == NULL ||
		!(loop->audio_options->effects_volume >= 0 &&
			loop->audio_options->effects_volume <= 1) ||
		!(loop->audio_options->music_volume >= 0 &&
			loop->audio_options->music_volume <= 1)) {
		mh_error_set(&loop->error, MH_ERROR_INVALID,
			"Invalid audio options");
		fprintf(stderr, "Invalid audio options\n");
		return 0;
	}

	return 1;
}

static int
start_process_clock(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	loop->process_clock = loop->capture == NULL &&
		!loop->field->cars[0].frame.active &&
		loop->field->cars[0].ticks == 0;

	if (loop->process_clock) {
		status = mh_game_timer_start(loop->timer, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Game clock: %s\n",
				loop->error.message);
			return 0;
		}
	}

	return 1;
}

static int
configure_viewport(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;

	scene = &loop->context->scene;
	assets = &scene->assets;

	loop->width = assets->view_width;
	loop->height = assets->view_height;

	if (loop->width < MH_VIEW_MIN_WIDTH ||
		loop->width > MH_VIEW_MAX_WIDTH ||
		loop->height < MH_VIEW_MIN_HEIGHT ||
		loop->height > MH_VIEW_MAX_HEIGHT) {
		mh_error_set(&loop->error, MH_ERROR_INVALID,
			"Invalid race viewport");
		fprintf(stderr, "Invalid race viewport\n");
		return 0;
	}

	return 1;
}

static int
apply_saved_settings(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;

	/* Profiles without Glenz use their Reflection material. */
	if (loop->context->settings != NULL &&
		(assets->profile == MH_RENDER_SOFTWARE ||
			assets->profile == MH_RENDER_POWERVR ||
			assets->profile == MH_RENDER_OPENMH) &&
		loop->context->settings->car_shading == MH_AV_SHADING_GLENZ)
		loop->context->settings->car_shading = MH_AV_SHADING_REFLECTION;

	loop->effective_audio = *loop->audio_options;

	if (loop->context->settings != NULL) {
		if (loop->context->settings->info_detail > MH_AV_INFO_ALL ||
			loop->context->settings->checkpoint_info >
				MH_AV_INFO_ALL ||
			loop->context->settings->road_map > MH_AV_INFO_ALL ||
			loop->context->settings->checkpoint_delay >
				MH_AV_CHECKPOINT_DELAY_MAX) {
			mh_error_set(&loop->error, MH_ERROR_INVALID,
				"Invalid HUD options");
			fprintf(stderr, "Invalid HUD options\n");
			return 0;
		}

		if (loop->context->settings->lens_flares > 1 ||
			loop->context->settings->halos > 1 ||
			loop->context->settings->smoke > 1 ||
			loop->context->settings->sparks > 1 ||
			loop->context->settings->skid_marks > 1 ||
			loop->context->settings->shadow > 1 ||
			loop->context->settings->sky_image > 1) {
			mh_error_set(&loop->error, MH_ERROR_INVALID,
				"Invalid effect options");
			fprintf(stderr, "Invalid effect options\n");
			return 0;
		}

		if (loop->context->settings->name_plates >
			MH_AV_NAMES_TRANSPARENT) {
			mh_error_set(&loop->error, MH_ERROR_INVALID,
				"Invalid nameplate option");
			fprintf(stderr, "Invalid nameplate option\n");
			return 0;
		}

		loop->menu_settings.shading =
			loop->context->settings->car_shading;
		loop->menu_settings.perspective =
			loop->context->settings->perspective;
		loop->menu_settings.distance =
			loop->context->settings->view_distance;
		loop->menu_settings.effects_volume =
			loop->context->settings->effects_volume;
		loop->menu_settings.cd_volume =
			loop->context->settings->music_volume;
		mh_pause_init(&loop->menu, NULL);
		memset(&loop->menu_input, 0, sizeof(loop->menu_input));
		status = mh_pause_update_glenz(&loop->menu,
			&loop->menu_settings, &loop->menu_input,
			assets->profile != MH_RENDER_SOFTWARE &&
				loop->menu_settings.shading ==
					MH_AV_SHADING_GLENZ,
			&loop->menu_output, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Driving settings: %s\n",
				loop->error.message);
			return 0;
		}

		/*
		 * An explicit CLI opt-in takes precedence over saved
		 * preferences.
		 */
		if (loop->audio_options->spatial == MH_RACE_AUDIO_SOFTWARE)
			loop->effective_audio.spatial =
				loop->context->settings->race_audio;

		loop->effective_audio.effects_volume =
			(loop->context->settings->effects_volume * 100 /
				MH_AV_VOLUME_MAX) /
			100.0;
		loop->effective_audio.music_volume =
			loop->context->settings->music_volume /
			(double)MH_AV_VOLUME_MAX;
		loop->effective_audio.no_music_loop =
			!loop->context->settings->cd_loop;
	}

	loop->effective_audio.no_scenery = !assets->scenery;
	loop->audio_options = &loop->effective_audio;

	return 1;
}

static void
configure_diagnostics(struct mh_race_loop *loop)
{
	loop->debug_glenz = loop->debug.glenz &&
		loop->context->scene.assets.profile != MH_RENDER_SOFTWARE;
	loop->debug_fixed = loop->debug.fixed_chase;
	loop->debug_trace = loop->debug.camera_trace;
	loop->debug_hidden = loop->debug.hidden_trace;

	if (loop->debug_fixed)
		fprintf(stderr, "Diagnostic fixed chase camera enabled\n");

	if (loop->debug_trace)
		fprintf(stderr,
			"camera-frame columns: elapsed steps remainder ticks mode paused car_xyz eye_xyz forward_xyz intro_ms\n");
}

static int
prepare_track_map(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	status = mh_race_context_load_map(
		loop->root, loop->context, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Track map: %s\n", loop->error.message);
		return 0;
	}

	return 1;
}

static int
bind_session(struct mh_race_loop *loop)
{
	loop->save_failed = 0;
	loop->live = loop->capture == NULL && loop->session != NULL;
	loop->continuous = loop->live && !loop->session->pause_with_ui;
	loop->chat_enabled =
		loop->session != NULL && loop->session->chat != NULL;

	if (loop->chat_enabled &&
		(loop->session->chat_config == NULL ||
			loop->session->send_chat == NULL)) {
		mh_error_set(&loop->error, MH_ERROR_INVALID,
			"Missing race chat callbacks");
		fprintf(stderr, "Missing race chat callbacks\n");
		return 0;
	}

	if (loop->live && loop->session->advance == NULL) {
		mh_error_set(&loop->error, MH_ERROR_INVALID,
			"Missing live session callback");
		fprintf(stderr, "Missing live session callback\n");
		return 0;
	}

	loop->drive = &loop->field->cars[0];
	loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];
	loop->mesh_count = 1 + MH_CAR_MESHES * loop->field->count;

	return 1;
}

static int
restore_camera_view(struct mh_race_loop *loop)
{
	unsigned int view;

	loop->camera_preference = MH_CAMERA_IN_CAR;

	if (loop->context->settings == NULL)
		return 1;

	view = loop->context->settings->car_view;

	if (view > MH_CAR_VIEW_OUTSIDE) {
		mh_error_set(&loop->error, MH_ERROR_INVALID,
			"Invalid saved car view");
		fprintf(stderr, "Invalid saved car view\n");
		return 0;
	}

	loop->camera_preference =
		view == MH_CAR_VIEW_INSIDE ? MH_CAMERA_IN_CAR : MH_CAMERA_CHASE;

	mh_race_restore_camera(loop);

	return 1;
}

static int
apply_view_settings(struct mh_race_loop *loop)
{
	enum mh_error_code status;
	unsigned int entry;

	if (loop->context->settings != NULL) {
		for (entry = 0; entry < loop->field->count; entry++) {
			status = mh_drive_view_distance(
				&loop->field->cars[entry],
				loop->context->settings->view_distance,
				&loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "View distance: %s\n",
					loop->error.message);
				return 0;
			}

			loop->field->cars[entry].race.split_hold_ms =
				loop->context->settings->checkpoint_delay;
		}
	}

	return 1;
}

static int
prepare_scene_effects(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	loop->tron = scene->assets.profile != MH_RENDER_SOFTWARE &&
		(loop->drive->cheats & MH_CHEAT_TRON) != 0;
	loop->retro = scene->assets.profile != MH_RENDER_SOFTWARE &&
		(loop->drive->cheats & MH_CHEAT_RETRO) != 0;

	status = mh_race_load_finish(loop, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Finish signs: %s\n", loop->error.message);
		return 0;
	}

	if (scene->assets.profile != MH_RENDER_SOFTWARE &&
		scene->assets.profile != MH_RENDER_POWERVR &&
		scene->assets.profile != MH_RENDER_OPENMH &&
		(loop->debug_glenz ||
			(loop->context->settings != NULL &&
				loop->context->settings->car_shading ==
					MH_AV_SHADING_GLENZ))) {
		status = mh_race_scene_load_glenz(loop->context->resources,
			loop->root, scene, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Glenz maps: %s\n",
				loop->error.message);
			return 0;
		}

		loop->glenz_loaded = 1;
		fprintf(stderr, "Glenz materials loaded (%u-bit maps)\n",
			scene->assets.opaque_format == MH_TEXTURE_RGB565 ? 16U
									: 32U);
	}

	if (loop->retro) {
		status = mh_race_scene_load_retro_from(loop->retro_source,
			loop->context->resources, loop->root, scene,
			&loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Retro assets: %s\n",
				loop->error.message);
			return 0;
		}
	}

	visuals->weather = NULL;
	visuals->pause_dim = 0;
	status = mh_race_scene_pause(
		scene, 0, loop->context->settings, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "World lighting: %s\n", loop->error.message);
		return 0;
	}

	if (scene->assets.profile != MH_RENDER_SOFTWARE) {
		status = mh_race_scene_prepare_lamps(
			scene, loop->field, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Attached lamps: %s\n",
				loop->error.message);
			return 0;
		}
	}

	status = mh_race_scene_names_prepare_from(loop->context->font_source,
		loop->context->resources, loop->root, scene, loop->field,
		loop->context->results.profiles, loop->context->results.count,
		&loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Car names: %s\n", loop->error.message);
		return 0;
	}

	loop->names_mode = loop->context->settings == NULL
		? MH_AV_NAMES_NONE
		: loop->context->settings->name_plates;

	return 1;
}

static int
open_window(struct mh_race_loop *loop)
{
	struct mh_window_system_options window_options;

	window_options.offscreen = loop->headless;
	window_options.preserve_environment = 0;

	if (mh_window_system_acquire(loop->windows, &window_options,
		&loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Video: %s\n", loop->error.message);
		return 0;
	}

	if (loop->loading != NULL) {
		if (loop->headless ||
			mh_loading_view_take_window(loop->loading,
				loop->windows,
				loop->context->scene.assets.profile,
				loop->width, loop->height, &loop->window,
				&loop->owns_window,
				&loop->error) != MH_ERROR_NONE) {
			fprintf(stderr, "Cannot take loading window\n");
			mh_error_copy(&loop->error, &loop->error,
				loop->error_text, sizeof(loop->error_text));
			mh_window_system_release(loop->windows);
			return 0;
		}

		loop->loading = NULL;

		return 1;
	}

	if (loop->window != NULL)
		return 1;

	loop->owns_window = !loop->headless;

	if (!loop->headless &&
		mh_window_open(loop->windows,
			mh_window_title(loop->window, "Motorhead",
				"Motorhead native driving"),
			loop->width, loop->height,
			MH_WINDOW_RESIZABLE |
				(loop->debug.window_titles
						? MH_WINDOW_DIAGNOSTIC_TITLES
						: 0) |
				(loop->context->settings != NULL &&
							loop->context->settings
								->fullscreen
						? MH_WINDOW_FULLSCREEN
						: 0),
			&loop->window, &loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Window: %s\n", loop->error.message);
		mh_error_copy(&loop->error, &loop->error, loop->error_text,
			sizeof(loop->error_text));
		mh_window_system_release(loop->windows);
		return 0;
	}

	return 1;
}

static void
prepare_render_inputs(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	struct mh_race_visuals *visuals;
	unsigned int i;

	scene = &loop->context->scene;
	assets = &scene->assets;
	visuals = &scene->visuals;

	for (i = 0; i < loop->mesh_count; i++)
		loop->meshes[i] = &assets->meshes[i];

	loop->meshes[loop->mesh_count + MH_SCENE_GPU_SKID_MESH] =
		&visuals->skid_mesh;
	loop->meshes[loop->mesh_count + MH_SCENE_GPU_PARTICLE_MESH] =
		&visuals->particle_mesh;
	loop->meshes[loop->mesh_count + MH_SCENE_GPU_SKY_MESH] =
		&visuals->sky_mesh;
	loop->meshes[loop->mesh_count + MH_SCENE_GPU_LAMP_MESH] =
		&visuals->lamps.mesh;
	loop->meshes[loop->mesh_count + MH_SCENE_GPU_SHADOW_MESH] =
		&visuals->shadow_mesh;
	loop->meshes[loop->mesh_count + MH_SCENE_GPU_NAME_MESH] =
		&visuals->names.mesh;
	loop->finish_mesh = loop->mesh_count +
		(assets->profile == MH_RENDER_SOFTWARE
				? MH_SCENE_SOFTWARE_EFFECT_MESHES
				: MH_SCENE_GPU_EFFECT_MESHES);

	for (i = 0; i < loop->finish_count; i++)
		loop->meshes[loop->finish_mesh + i] = &assets->finish.meshes[i];

	memset(&loop->sky_state, 0, sizeof(loop->sky_state));
	memset(loop->models + loop->mesh_count * 16, 0, 16 * sizeof(float));

	for (i = 0; i < 4; i++)
		loop->models[loop->mesh_count * 16 + i * 5] = 1;

	for (i = 1; i < MH_SCENE_GPU_EFFECT_MESHES; i++)
		memcpy(loop->models + (loop->mesh_count + i) * 16,
			loop->models + loop->mesh_count * 16,
			16 * sizeof(float));

	for (i = 0; i < loop->finish_count; i++)
		memcpy(loop->models + (loop->finish_mesh + i) * 16,
			loop->models + loop->mesh_count * 16,
			16 * sizeof(float));

	memset(&loop->particle_camera, 0, sizeof(loop->particle_camera));
	loop->particle_camera.near_depth = MH_CAMERA_RACE_NEAR;
	loop->particle_camera.far_depth = mh_drive_camera_far(loop->viewed);
	loop->particle_camera.width = loop->width;
	loop->particle_camera.height = loop->height;
	loop->particle_camera.brightness = visuals->brightness.particle;
	loop->particle_camera.smoke = loop->particle_camera.sparks = 1;

	if (loop->context->settings != NULL) {
		loop->particle_camera.smoke =
			(int)loop->context->settings->smoke;
		loop->particle_camera.sparks =
			(int)loop->context->settings->sparks;
	}
}

static enum mh_error_code
configure_blur_history(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int frames;

	frames = loop->context->settings != NULL &&
			loop->context->settings->triple_buffer
		? 3
		: 2;
	status =
		mh_renderer_set_blur_history(loop->renderer, frames, error_out);

	/* Original DirectDraw allocation retries with one back buffer. */
	if (status != MH_ERROR_NONE && frames == 3) {
		fprintf(stderr,
			"Three-buffer history unavailable; retrying two buffers\n");
		frames = 2;
		status = mh_renderer_set_blur_history(
			loop->renderer, frames, error_out);
	}

	if (status == MH_ERROR_NONE)
		fprintf(stderr, "Race blur history: %u\n", frames);

	return status;
}

static enum mh_error_code
configure_frame_queue(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int frames;

	frames = loop->context->settings->frame_queue + 1;
	status = mh_renderer_set_frame_queue(loop->renderer, frames, error_out);

	if (status == MH_ERROR_UNSUPPORTED) {
		/*
		 * Browser presentation queues are controlled by the compositor.
		 */
		fprintf(stderr, "Race frame queue control unavailable\n");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status == MH_ERROR_NONE)
		fprintf(stderr, "Race frame queue: %u\n", frames);

	return status;
}

static enum mh_error_code
configure_gpu_renderer(struct mh_race_loop *loop, struct mh_error *error_out)
{
	const struct mh_race_scene_assets *assets;
	enum mh_error_code status;

	assets = &loop->context->scene.assets;
	status = mh_race_scene_gpu_order(loop->renderer, &loop->context->scene,
		loop->field->count, loop->finish_count, error_out);

	if (status == MH_ERROR_NONE && loop->context->settings != NULL)
		status = mh_renderer_set_texture_filter(loop->renderer,
			assets->profile == MH_RENDER_POWERVR
				? MH_TEXTURE_FILTER_ORIGINAL
				: (enum mh_texture_filter)loop->context
					->settings->texture_filter,
			error_out);

	if (status == MH_ERROR_NONE && assets->profile == MH_RENDER_DIRECT3D)
		status = configure_blur_history(loop, error_out);

	if (status == MH_ERROR_NONE && loop->window != NULL &&
		loop->context->settings != NULL)
		status = configure_frame_queue(loop, error_out);

	return status;
}

static void
configure_vsync(struct mh_race_loop *loop)
{
	struct mh_error error;
	int enabled;

	if (loop->window == NULL)
		return;

	enabled = loop->context->settings == NULL ||
		loop->context->settings->vsync;

	if (loop->session != NULL && loop->session->benchmark)
		enabled = 0;

	if (mh_renderer_set_vsync(loop->renderer, enabled, &error) !=
		MH_ERROR_NONE)
		fprintf(stderr, "Race VSync request unavailable: %s\n",
			error.message);
	else
		fprintf(stderr, "Race VSync: %s\n", enabled ? "On" : "Off");
}

static void
discard_presentation(struct mh_race_loop *loop)
{
	mh_error_copy(&loop->error, &loop->error, loop->error_text,
		sizeof(loop->error_text));
	mh_renderer_destroy(loop->renderer);

	if (loop->owns_window)
		mh_window_destroy(loop->window);

	mh_window_system_release(loop->windows);
	loop->renderer = NULL;
	loop->window = NULL;
}

static int
open_renderer(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;

	if (MH_RENDER_BUILD_SOFTWARE && assets->profile == MH_RENDER_SOFTWARE) {
		if (mh_race_open_software(loop, &loop->error) !=
			MH_ERROR_NONE) {
			fprintf(stderr, "Software renderer: %s\n",
				loop->error.message);
			discard_presentation(loop);
			return 0;
		}
	} else {
		if (mh_renderer_open(mh_render_set_profile(
					loop->renderers, assets->profile),
			loop->window, loop->meshes,
			loop->finish_mesh + loop->finish_count, loop->width,
			loop->height,
			loop->debug.gpu ? MH_RENDERER_DIAGNOSTICS : 0,
			&loop->renderer, &loop->error) != MH_ERROR_NONE) {
			fprintf(stderr, "Renderer: %s\n", loop->error.message);
			discard_presentation(loop);
			return 0;
		}

		status = configure_gpu_renderer(loop, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Scene order: %s\n",
				loop->error.message);
			discard_presentation(loop);
			return 0;
		}
	}

	configure_vsync(loop);
	printf("Race viewport: %ux%u\n", loop->width, loop->height);
	printf("Race renderer: %s\n",
		loop->context->scene.assets.profile == MH_RENDER_SOFTWARE
			? "Software"
			: assets->profile == MH_RENDER_DIRECT3D ? "Direct3D"
			: assets->profile == MH_RENDER_POWERVR	? "PowerVR"
			: assets->profile == MH_RENDER_OPENMH	? "OpenMH"
								: "Glide");
	fflush(stdout);

	if (!mh_renderer_set_motion_blur(loop->renderer,
		loop->context->settings != NULL &&
			loop->context->settings->motion_blur != 0)) {
		mh_renderer_error(loop->renderer, &loop->error);
		fprintf(stderr, "Motion blur: %s\n", loop->error.message);
		discard_presentation(loop);
		return 0;
	}

	if (assets->profile != MH_RENDER_SOFTWARE &&
		assets->track.has_cue_color) {
		status = mh_fog_configure(assets->track.cue_color,
			mh_drive_camera_far(loop->viewed), &loop->fog,
			&loop->error);

		if (status != MH_ERROR_NONE ||
			!mh_renderer_set_fog(loop->renderer, &loop->fog)) {
			if (status == MH_ERROR_NONE)
				mh_renderer_error(loop->renderer, &loop->error);

			fprintf(stderr, "Fog: %s\n", loop->error.message);
			discard_presentation(loop);

			return 0;
		}
	}

	if (!mh_renderer_ui_upload(
		loop->renderer, mh_race_context_atlas(loop->context))) {
		mh_renderer_error(loop->renderer, &loop->error);
		fprintf(stderr, "HUD upload: %s\n", loop->error.message);
		discard_presentation(loop);
		return 0;
	}

	return 1;
}

static int
open_input(struct mh_race_loop *loop)
{
	const struct mh_av_settings *settings;
	enum mh_error_code status;

	settings = loop->context->settings;
	memset(&loop->devices, 0, sizeof(loop->devices));
	status = mh_input_open_from(loop->inputs, &loop->devices,
		loop->capture == NULL ? loop->window : NULL,
		settings == NULL || settings->fullscreen ||
			settings->grab_mouse,
		&loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Input devices: %s\n", loop->error.message);
		discard_presentation(loop);
		return 0;
	}

	return 1;
}

static unsigned int
pause_mode(const struct mh_race_loop *loop)
{
	if (loop->continuous)
		return MH_GAME_NETWORK;

	/*
	 * The older results wrapper is League; the local handoff supplies quit.
	 */
	if ((loop->result_handler != NULL && loop->quit == NULL) ||
		(loop->session != NULL && loop->session->league))
		return MH_GAME_LEAGUE;

	if (loop->field->attract)
		return MH_GAME_ATTRACT;

	if (loop->field->playback != 0)
		return (unsigned int)loop->field->playback;

	return loop->field->time_attack ? MH_GAME_TIME_ATTACK
					: MH_GAME_QUICK_RACE;
}

static void
reset_presentation(struct mh_race_loop *loop)
{
	unsigned int i;

	loop->input_mode = 0;
	loop->screenshot.renderer = loop->renderer;
	mh_messages_init(&loop->local_messages);
	mh_messages_width(&loop->local_messages,
		loop->width - 10 < 540 ? loop->width - 10 : 540, NULL);
	loop->local_messages_present = loop->screenshot_failed = 0;
	loop->previous = mh_clock_source_counter(loop->timer->source);
	loop->running = loop->success = loop->focused = loop->reflections = 1;
	loop->paused = loop->results_shown = loop->results_ready = 0;
	loop->map_mode = loop->gap_mode = MH_AV_INFO_ALL;

	if (loop->context->settings != NULL) {
		loop->map_mode = loop->context->settings->road_map;
		loop->gap_mode = loop->context->settings->checkpoint_info;
	}

	/*
	 * Original counters, phases and Club TBL latch live for the process;
	 * loading a new HUD clears its notice and binds a new scroll resource.
	 */
	loop->history->hud.notice = 0;
	loop->history->hud.scroll_started = 0;
	memset(&loop->screen, 0, sizeof(loop->screen));
	loop->screen.visible = loop->results;
	mh_pause_init(&loop->menu, NULL);
	mh_keyboard_init(&loop->keyboard, NULL);
	memset(loop->down, 0, sizeof(loop->down));
	memset(&loop->shaped_controls, 0, sizeof(loop->shaped_controls));
	memset(&loop->chat_defaults, 0, sizeof(loop->chat_defaults));
	loop->chat_defaults.codes[MH_CONTROL_CHAT] = DEFAULT_CHAT_BINDING;
	loop->chat_defaults.codes[MH_CONTROL_DIGIT_0] = 0x0b;

	for (i = 1; i < 10; i++)
		loop->chat_defaults.codes[MH_CONTROL_DIGIT_0 + i] = i + 1;

	memset(&loop->menu_input, 0, sizeof(loop->menu_input));
	memset(&loop->menu_output, 0, sizeof(loop->menu_output));
	memset(&loop->menu_settings, 0, sizeof(loop->menu_settings));
	loop->menu_input.mode = pause_mode(loop);

	loop->menu_input.restart_ok = 1;
	loop->menu_settings.shading = MH_AV_SHADING_REFLECTION;
	loop->menu_settings.perspective = MH_AV_PERSPECTIVE_GOOD;
	loop->menu_settings.distance = 100;
	loop->menu_settings.effects_volume =
		(unsigned int)(loop->audio_options->effects_volume *
				MH_PAUSE_VOLUME_MAX +
			.5);
	loop->menu_settings.cd_volume =
		(unsigned int)(loop->audio_options->music_volume *
				MH_PAUSE_VOLUME_MAX +
			.5);

	if (loop->context->settings != NULL) {
		loop->menu_settings.shading =
			loop->context->settings->car_shading;
		loop->menu_settings.perspective =
			loop->context->settings->perspective;
		loop->menu_settings.distance =
			loop->context->settings->view_distance;
		loop->menu_settings.effects_volume =
			loop->context->settings->effects_volume;
		loop->menu_settings.cd_volume =
			loop->context->settings->music_volume;
		loop->reflections =
			loop->menu_settings.shading >= MH_AV_SHADING_REFLECTION;
	}

	if (loop->continuous)
		mh_results_screen_update(
			&loop->screen, &loop->field->standings);

	loop->was_results = 1;
	loop->effective_audio.mode = loop->menu_input.mode;
}

static const char *
race_audio_name(int profile)
{
	switch (profile) {
	case MH_RACE_AUDIO_A3D20:
		return "Spatial (A3D 2.0)";
	case MH_RACE_AUDIO_A3D12:
		return "Spatial (A3D 1.2)";
	case MH_RACE_AUDIO_EAX:
		return "Spatial (EAX reverb)";
	case MH_RACE_AUDIO_DS3D:
		return "Spatial (DS3D)";
	default:
		return "Software";
	}
}

static void
open_audio(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;

	scene = &loop->context->scene;
	assets = &scene->assets;

	loop->audio = loop->capture == NULL
		? mh_audio_open_from(scene->horn_source, scene->material_source,
			scene->env_sound_source, scene->animation_source,
			loop->audio_provider, loop->audio_decoder, loop->root,
			&assets->track, &assets->lighting_world,
			loop->audio_options, loop->context->resources,
			loop->timer->source, loop->audio_trace)
		: NULL;

	if (loop->audio != NULL)
		fprintf(stderr, "Race audio: %s\n",
			race_audio_name(loop->audio_options->spatial));
}

static void
enter_process_clock(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	/*
	 * A late clock failure still leaves an owned loop for close to release.
	 */
	loop->previous = mh_clock_source_counter(loop->timer->source);

	if (loop->process_clock) {
		loop->current_counter = loop->previous;
		status = mh_game_timer_enter(loop->timer, loop->field,
			&loop->current_counter, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Race load clock: %s\n",
				loop->error.message);
			loop->success = loop->running = 0;
		}
	}
}

int
mh_race_initialize(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	memset(&loop->attract_frame, 0, sizeof(loop->attract_frame));
	loop->attract_phase = 0;

	if (!validate_audio_options(loop))
		return 0;

	if (!start_process_clock(loop))
		return 0;

	if (!configure_viewport(loop))
		return 0;

	if (!apply_saved_settings(loop))
		return 0;

	configure_diagnostics(loop);

	mh_race_scene_bind_cameras(scene, loop->field);

	if (!prepare_track_map(loop))
		return 0;

	if (!bind_session(loop))
		return 0;

	if (!restore_camera_view(loop))
		return 0;

	if (!apply_view_settings(loop))
		return 0;

	if (!prepare_scene_effects(loop))
		return 0;

	if (!open_window(loop))
		return 0;

	prepare_render_inputs(loop);

	if (!open_renderer(loop))
		return 0;

	if (loop->capture == NULL) {
		/*
		 * Every new renderer resets both animation timers. Explicit
		 * captures retain their pre-advanced diagnostic frame.
		 */
		visuals->tron_time = 0;
		visuals->retro_state = loop->history->retro;
		visuals->retro_state.time = 0;
	}

	if (!open_input(loop))
		return 0;

	reset_presentation(loop);

	open_audio(loop);

	enter_process_clock(loop);

	return 1;
}

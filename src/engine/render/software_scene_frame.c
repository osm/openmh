#include <string.h>

#include "engine/core/limits.h"
#include "engine/render/software_scene.h"
#include "engine/render/software_shadow_mesh.h"

static enum mh_error_code
configure_cue(const struct mh_race_scene *scene, struct mh_renderer *renderer,
	struct mh_error *error_out)
{
	const struct mh_track *track;
	const double *color;
	struct mh_depth_cue cue;
	unsigned int i;

	track = &scene->assets.track;

	if (!track->has_cue_color && scene->visuals.weather == NULL)
		return mh_renderer_set_depth_cue(renderer, NULL, error_out);

	memset(&cue, 0, sizeof(cue));
	cue.start = (float)track->cue_start;
	color = scene->visuals.weather == NULL ? track->cue_color
					: scene->visuals.weather->fog;

	for (i = 0; i < 3; i++)
		cue.color[i] = (float)(color[i] < 0    ? 0
				: color[i] > MH_U8_MAX ? MH_U8_MAX
						: color[i]);

	return mh_renderer_set_depth_cue(renderer, &cue, error_out);
}

static enum mh_error_code
light_models(struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_renderer *renderer,
	const struct mh_scene_software_frame *frame,
	struct mh_model_lighting *object, struct mh_error *error_out)
{
	struct mh_model_lighting light;
	struct mh_av_settings settings;
	unsigned int i, root;
	enum mh_error_code status;

	memset(&settings, 0, sizeof(settings));
	settings.car_shading = frame->shading;

	for (i = 0; i < (1 + MH_CAR_MESHES * field->count); i++) {
		status = mh_race_scene_software_lighting(
			scene, field, i, &settings, &light, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		root = i == 0 ? 0
			: 1 + ((i - 1) / MH_CAR_MESHES) * MH_CAR_MESHES;

		if (i != 0 || scene->assets.meshes[0].scenery != NULL)
			status = mh_renderer_world_lighting(renderer, i, root,
				&scene->assets.lighting_world, &light,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (i == 0)
			*object = light;
		else if (!mh_renderer_mesh_visible(renderer, i,
				mh_race_scene_part_visible(scene, field, i) &&
					(frame->visible_roots &
						(1U << ((i - 1) /
							MH_CAR_MESHES))) !=
						0))
			return mh_renderer_error(renderer, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_projection(const struct mh_race_scene *scene,
	const struct mh_field *field,
	const struct mh_scene_software_frame *frame, struct mh_world_view *view,
	struct mh_projection *projection, struct mh_error *error_out)
{
	struct mh_camera_lens lens;
	const struct mh_drive *viewed;
	enum mh_error_code status;

	viewed = &field->cars[mh_field_viewed(field)];

	status = mh_race_scene_world_view_profile(MH_RENDER_SOFTWARE, field,
		&scene->assets.lighting_world, frame->width, frame->height,
		view, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_camera_lens_setup(viewed->camera_goal ||
					viewed->camera_mode == MH_CAMERA_TRACK
				? viewed->camera_focal
				: MH_CAMERA_RACE_FOCAL,
			MH_CAMERA_RACE_NEAR, mh_drive_camera_far(viewed),
			(float)frame->width / frame->height, &lens, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_projection_setup(frame->width, frame->height,
			(float)frame->width / frame->height, lens.focal,
			lens.near_depth, lens.far_depth, projection, error_out);

	return status;
}

enum mh_error_code
mh_race_scene_software_frame(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_renderer *renderer,
	const struct mh_scene_software_frame *frame, struct mh_error *error_out)
{
	const struct mh_av_settings *settings;
	struct mh_world_view view;
	struct mh_projection projection;
	struct mh_model_lighting object;
	struct mh_renderer_effects effects;
	struct mh_renderer_skids skids;
	struct mh_renderer_particles particles;
	struct mh_renderer_halos halos;
	struct mh_renderer_names names;
	struct mh_renderer_name name_sources[MH_NAME_LABELS_MAX];
	int oldest[MH_RACE_MAX_CARS][4];
	unsigned int i, j;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || frame == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS ||
		frame->models == NULL || frame->view == NULL ||
		frame->width == 0 || frame->height == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software scene frame");

	settings = frame->settings;
	status = configure_cue(scene, renderer, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_renderer_set_texture_mapping(renderer,
			settings != NULL && settings->perspective_on
				? MH_TEXTURE_PERSPECTIVE
				: MH_TEXTURE_SELECTIVE,
			frame->perspective, error_out);

	if (status == MH_ERROR_NONE)
		status = light_models(
			scene, field, renderer, frame, &object, error_out);

	if (status == MH_ERROR_NONE)
		status = prepare_projection(
			scene, field, frame, &view, &projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (settings == NULL || settings->shadow)
		status = mh_software_shadow_mesh_update(
			&scene->visuals.shadow_mesh, field, frame->models,
			&view.frustum, error_out);
	else
		memset(scene->visuals.shadow_mesh.draw_hidden, 1,
			MH_RACE_MAX_CARS);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_scene_update_software_halos(
		scene, field, frame->models, &view, settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&effects, 0, sizeof(effects));
	memset(&names, 0, sizeof(names));
	names.sources = name_sources;
	names.transparent = frame->names_mode == MH_AV_NAMES_TRANSPARENT;
	status = mh_race_scene_software_names(scene, field, frame->models,
		frame->visible_roots, frame->names_mode != 0, name_sources,
		&names.count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	effects.names = &names;
	memset(&halos, 0, sizeof(halos));
	halos.sources = scene->visuals.software_halos.sources;
	halos.count = scene->visuals.software_halos.count;
	halos.lens = &scene->assets.software.lens;
	halos.cue_start = (float)scene->assets.track.cue_start;
	effects.halos = &halos;
	memset(&skids, 0, sizeof(skids));
	skids.histories = scene->visuals.skids;
	skids.cars = MH_RACE_MAX_CARS;
	skids.view = &view;
	skids.world = &scene->assets.lighting_world;
	skids.lighting = &object;
	skids.oldest_visible = oldest;

	if (settings == NULL || settings->skid_marks)
		effects.skids = &skids;

	memset(&particles, 0, sizeof(particles));
	particles.states = scene->visuals.particles;
	particles.assets = &scene->assets.software.particles;
	particles.frustum = &view.frustum;
	particles.cars = MH_RACE_MAX_CARS;
	particles.brightness = scene->visuals.brightness.particle;
	particles.smoke = settings == NULL || settings->smoke;
	particles.sparks = settings == NULL || settings->sparks;
	effects.particles = &particles;

	/* Software materials provide reflections within their ordinary pass. */
	status =
		mh_renderer_scene_frame_lens(renderer, &projection, frame->view,
			frame->models, 0, frame->ui, NULL, &effects, error_out);

	if (status == MH_ERROR_NONE && effects.skids != NULL)
		for (i = 0; i < MH_RACE_MAX_CARS; i++)
			for (j = 0; j < 4; j++)
				scene->visuals.skids[i]
					.wheels[j]
					.oldest_visible = oldest[i][j];

	return status;
}

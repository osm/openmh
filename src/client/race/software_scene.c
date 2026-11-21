#include "client/race/internal.h"
#include "engine/render/software_scene.h"
#include "interfaces/render/backend.h"

enum mh_error_code
mh_race_open_software(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_race_scene *scene;
	enum mh_error_code status;

	scene = &loop->context->scene;
	mh_field_matrices(loop->field, (double)loop->width / loop->height,
		loop->models, loop->matrix, loop->view);
	status = mh_race_scene_visibility_profile(MH_RENDER_SOFTWARE, scene,
		loop->field, loop->models, loop->width, loop->height,
		&loop->visible_roots, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_scene_prepare_software_halos(
		scene, loop->field->count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	loop->meshes[loop->mesh_count] = &scene->visuals.shadow_mesh;
	status = mh_renderer_open(
		mh_render_set_profile(loop->renderers, MH_RENDER_SOFTWARE),
		loop->window, loop->meshes,
		loop->finish_mesh + loop->finish_count, loop->width,
		loop->height, loop->debug.gpu ? MH_RENDERER_DIAGNOSTICS : 0,
		&loop->renderer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_scene_software_setup(scene, loop->renderer,
		loop->field->count, loop->finish_count, loop->context->settings,
		error_out);

	if (status != MH_ERROR_NONE) {
		if (error_out != NULL)
			mh_error_copy(error_out, error_out, loop->error_text,
				sizeof(loop->error_text));

		mh_renderer_destroy(loop->renderer);
		loop->renderer = NULL;
	}

	return status;
}

int
mh_race_draw_software_scene(struct mh_race_loop *loop)
{
	struct mh_scene_software_frame frame;

	if (!mh_race_prepare_hud(loop) || !mh_race_build_overlay(loop))
		return 0;

	frame.settings = loop->context->settings;
	frame.width = loop->width;
	frame.height = loop->height;
	frame.models = loop->models;
	frame.view = loop->view;
	frame.ui = &loop->ui;
	frame.visible_roots = loop->visible_roots;
	frame.shading = loop->menu_settings.shading;
	frame.perspective = loop->menu_settings.perspective;
	frame.names_mode = loop->names_mode;

	if (mh_race_scene_software_frame(&loop->context->scene, loop->field,
		loop->renderer, &frame, &loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Software scene: %s\n", loop->error.message);
		loop->success = loop->running = 0;
		return 0;
	}

	return 1;
}

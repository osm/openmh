#include "backends/render/webgl_error.h"
#include "backends/render/webgl_renderer.h"

static void
retain_history_settings(struct mh_webgl_renderer *renderer)
{
	if (renderer->restoring)
		return;

	renderer->restore_blur = renderer->history.blur;
	renderer->restore_frames = 0;

	if (renderer->history.back.target.framebuffer != 0)
		renderer->restore_frames = 2;

	if (renderer->history.third.target.framebuffer != 0)
		renderer->restore_frames = 3;

	renderer->restoring = 1;
}

static void
release_resources(struct mh_webgl_renderer *renderer)
{
	glUseProgram(0);
	mh_webgl_ui_release(renderer->ui);
	mh_webgl_scene_release(renderer->scene);
	mh_webgl_drawing_destroy(&renderer->drawing);
	mh_webgl_history_destroy(&renderer->history);

	/* These scratch passes rebuild lazily from the next frame's input. */
	mh_webgl_depth_destroy(&renderer->depth);
	mh_webgl_retro_destroy(&renderer->retro);
	mh_webgl_tron_destroy(&renderer->tron);
	mh_webgl_materials_destroy(&renderer->materials);

	/*
	 * Retire Emscripten handles before allocating any replacement objects.
	 */
	mh_webgl_discard_lost_object_errors();
}

static enum mh_error_code
restore_history(struct mh_webgl_renderer *renderer, struct mh_error *error_out)
{
	enum mh_error_code status;

	/*
	 * Lost frame contents cannot be recovered; start with cleared history.
	 */
	status = mh_webgl_history_create(&renderer->history, renderer->width,
		renderer->height, error_out);

	if (status == MH_ERROR_NONE && renderer->restore_frames != 0)
		status = mh_webgl_history_frames(&renderer->history,
			renderer->restore_frames, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_history_blur(
			&renderer->history, renderer->restore_blur, error_out);

	return status;
}

enum mh_error_code
mh_webgl_renderer_restore(void *context, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	renderer = context;
	retain_history_settings(renderer);
	release_resources(renderer);
	status = mh_webgl_drawing_create(&renderer->drawing, error_out);

	if (MH_RENDER_BUILD_OPENMH && status == MH_ERROR_NONE &&
		renderer->base.backend->profile == MH_RENDER_OPENMH)
		status = mh_webgl_drawing_openmh(&renderer->drawing, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_scene_restore(renderer->scene, error_out);

	if (status == MH_ERROR_NONE && renderer->ui != NULL)
		status = mh_webgl_ui_restore(renderer->ui, error_out);

	if (status == MH_ERROR_NONE)
		status = restore_history(renderer, error_out);

	if (status == MH_ERROR_NONE)
		renderer->restoring = 0;

	return status;
}

#include <stdio.h>
#include <stdlib.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_renderer.h"
#include "engine/render/viewport.h"

enum {
	SCENE_MESHES_MAX = 128
};

void
mh_webgl_renderer_destroy(struct mh_renderer *base)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	renderer = (struct mh_webgl_renderer *)base;

	if (renderer == NULL)
		return;

	if (renderer->context.handle != 0) {
		renderer->context.restore = NULL;
		status = mh_webgl_context_current(&renderer->context, NULL);
		glUseProgram(0);
		mh_webgl_ui_destroy(renderer->ui);
		mh_webgl_depth_destroy(&renderer->depth);
		mh_webgl_retro_destroy(&renderer->retro);
		mh_webgl_tron_destroy(&renderer->tron);
		mh_webgl_materials_destroy(&renderer->materials);
		mh_webgl_scene_close(renderer->scene);
		mh_webgl_drawing_destroy(&renderer->drawing);
		mh_webgl_history_destroy(&renderer->history);

		if (status != MH_ERROR_NONE)
			mh_webgl_discard_lost_object_errors();

		mh_webgl_context_close(&renderer->context);
	}

	free(renderer->ui);
	mh_tron_scene_destroy(&renderer->tron_scene);
	free(renderer);
}

enum mh_error_code
mh_webgl_renderer_open(struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, int powervr,
	struct mh_renderer **output, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	if (output == NULL || count > SCENE_MESHES_MAX ||
		(count != 0 && meshes == NULL) || width < MH_VIEW_MIN_WIDTH ||
		width > MH_VIEW_MAX_WIDTH || height < MH_VIEW_MIN_HEIGHT ||
		height > MH_VIEW_MAX_HEIGHT ||
		(flags & ~MH_RENDERER_DIAGNOSTICS) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL renderer dimensions or scene");

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"WebGL renderer requires a browser window");

	renderer = calloc(1, sizeof(*renderer));

	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL renderer");

	renderer->width = width;
	renderer->height = height;
	status = mh_webgl_context_open(&renderer->context, window, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_webgl_drawing_create(&renderer->drawing, error_out);

		/* Shader diagnostics otherwise borrow the failed renderer. */
		if (status != MH_ERROR_NONE) {
			if (error_out != NULL && error_out->message != NULL)
				fprintf(stderr, "WebGL shaders: %s\n",
					error_out->message);

			mh_error_set(error_out, status,
				"cannot create WebGL scene programs");
		}
	}

	if (status == MH_ERROR_NONE)
		status = mh_webgl_scene_open(
			meshes, count, powervr, &renderer->scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_history_create(
			&renderer->history, width, height, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_renderer_destroy(&renderer->base);
		return status;
	}

	if ((flags & MH_RENDERER_DIAGNOSTICS) != 0)
		fprintf(stderr, "WebGL renderer: %s; %s\n",
			glGetString(GL_RENDERER), glGetString(GL_VERSION));

	renderer->context.restore = mh_webgl_renderer_restore;
	renderer->context.restore_context = renderer;
	*output = &renderer->base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_renderer_ui(struct mh_webgl_renderer *renderer,
	const struct mh_ui_atlas *atlas, struct mh_error *error_out)
{
	struct mh_webgl_ui *ui;
	enum mh_error_code status;

	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	ui = calloc(1, sizeof(*ui));

	if (ui == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL UI resources");

	status = mh_webgl_ui_create(
		ui, atlas, renderer->width, renderer->height, error_out);

	if (status != MH_ERROR_NONE) {
		if (error_out != NULL)
			mh_error_copy(error_out, error_out,
				renderer->diagnostic,
				sizeof(renderer->diagnostic));

		free(ui);

		return status;
	}

	mh_webgl_ui_destroy(renderer->ui);
	free(renderer->ui);
	renderer->ui = ui;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

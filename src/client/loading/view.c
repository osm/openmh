#include <stdlib.h>

#include "client/diagnostics/options.h"
#include "client/loading/view.h"
#include "engine/ui/loading_assets.h"
#include "engine/window/title.h"
#include "interfaces/render/backend.h"

struct mh_loading_view {
	const struct mh_window_system	*windows;
	struct mh_window		*window;
	struct mh_renderer		*renderer;
	struct mh_loading_assets	assets;
	struct mh_loading_layout	layout;
	struct mh_loading_progress	progress;
	struct mh_ui_list		list;
	unsigned int			width, height;
	int				owns_window;
	unsigned int			references;
	struct mh_loading_confirmation	confirmation;
};

void
mh_loading_view_close(struct mh_loading_view *view)
{
	if (view == NULL)
		return;

	if (--view->references != 0)
		return;

	mh_loading_confirmation_close(&view->confirmation);
	mh_renderer_destroy(view->renderer);

	if (view->owns_window)
		mh_window_destroy(view->window);

	mh_loading_assets_free(&view->assets);

	if (view->windows != NULL)
		mh_window_system_release(view->windows);

	free(view);
}

enum mh_error_code
mh_loading_view_open(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_window_system *windows, struct mh_window *window,
	const struct mh_render_backend *backend, unsigned int width,
	unsigned int height, int fullscreen,
	const struct mh_loading_layout *layout,
	const struct mh_debug_options *debug, struct mh_loading_view **output,
	struct mh_error *error_out)
{
	struct mh_loading_view *view;
	enum mh_error_code status;

	if (resources == NULL || root == NULL || windows == NULL ||
		backend == NULL || layout == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading view request");

	view = (struct mh_loading_view *)calloc(1, sizeof(*view));

	if (view == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening loading view");

	view->references = 1;
	view->width = width;
	view->height = height;
	view->layout = *layout;
	mh_loading_progress_init(&view->progress);
	status = mh_loading_assets_load_from(fonts, decoder, resources, root,
		backend->profile, width, height, &view->assets, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_window_system_acquire(windows, NULL, error_out);

	if (status == MH_ERROR_NONE) {
		view->windows = windows;
		view->window = window;
		view->owns_window = window == NULL;
	}

	if (status == MH_ERROR_NONE && view->owns_window)
		status = mh_window_open(windows,
			debug != NULL && debug->window_titles
				? "Motorhead loading"
				: "Motorhead",
			width, height,
			MH_WINDOW_RESIZABLE |
				(debug != NULL && debug->window_titles
						? MH_WINDOW_DIAGNOSTIC_TITLES
						: 0) |
				(fullscreen ? MH_WINDOW_FULLSCREEN : 0),
			&view->window, error_out);
	else if (status == MH_ERROR_NONE)
		status = mh_window_set_title(window,
			mh_window_title(
				view->window, "Motorhead", "Motorhead loading"),
			error_out);

	if (status == MH_ERROR_NONE && window != NULL && !fullscreen) {
		status = mh_window_set_size(window, width, height, error_out);

		if (status == MH_ERROR_UNSUPPORTED)
			status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status == MH_ERROR_NONE)
		status = mh_renderer_open(backend, view->window, NULL, 0, width,
			height,
			debug != NULL && debug->gpu ? MH_RENDERER_DIAGNOSTICS
						: 0,
			&view->renderer, error_out);

	if (status == MH_ERROR_NONE) {
		mh_ui_begin(&view->list, width, height);
		status = mh_loading_draw(
			&view->assets, layout, &view->list, error_out);
	}

	if (status == MH_ERROR_NONE &&
		(!mh_renderer_ui_upload(view->renderer, &view->assets.atlas) ||
			!mh_renderer_ui_frame(view->renderer, &view->list)))
		status = mh_renderer_error(view->renderer, error_out);

	if (status != MH_ERROR_NONE) {
		mh_loading_view_close(view);

		/* Backend messages may belong to the renderer just released. */

		return mh_error_set(
			error_out, status, "cannot present loading screen");
	}

	*output = view;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_loading_view_step(struct mh_loading_view *view, unsigned long milliseconds,
	struct mh_error *error_out)
{
	struct mh_loading_progress progress;
	enum mh_error_code status;

	if (view == NULL || view->renderer == NULL)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "loading view is not open");

	progress = view->progress;
	status = mh_loading_progress_step(&progress, milliseconds, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (view->assets.profile == MH_RENDER_SOFTWARE) {
		mh_ui_begin(&view->list, view->width, view->height);
		status = mh_loading_draw(
			&view->assets, &view->layout, &view->list, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_loading_draw_progress(&view->assets,
				&progress, &view->list, error_out);

		if (status == MH_ERROR_NONE &&
			!mh_renderer_ui_frame(view->renderer, &view->list))
			status = mh_renderer_error(view->renderer, error_out);
	}

	if (status == MH_ERROR_NONE)
		view->progress = progress;

	return status;
}

enum mh_error_code
mh_loading_view_take_window(struct mh_loading_view *view,
	const struct mh_window_system *windows, enum mh_render_profile profile,
	unsigned int width, unsigned int height, struct mh_window **output,
	int *owns_window, struct mh_error *error_out)
{
	if (view == NULL || output == NULL || owns_window == NULL ||
		view->window == NULL || view->windows != windows ||
		view->assets.profile != profile || view->width != width ||
		view->height != height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incompatible loading window handoff");

	mh_loading_confirmation_close(&view->confirmation);
	mh_renderer_destroy(view->renderer);
	view->renderer = NULL;
	*output = view->window;
	*owns_window = view->owns_window;
	view->owns_window = 0;
	view->window = NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_loading_view_retain(struct mh_loading_view *view)
{
	view->references++;
}

enum mh_error_code
mh_loading_view_ready(struct mh_loading_view *view,
	const struct mh_input_provider *inputs, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (view == NULL || view->renderer == NULL ||
		view->confirmation.input.backend != NULL)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "loading view is not ready");

	mh_ui_begin(&view->list, view->width, view->height);
	status = mh_loading_draw(
		&view->assets, &view->layout, &view->list, error_out);

	if (status == MH_ERROR_NONE &&
		view->assets.profile == MH_RENDER_SOFTWARE)
		status = mh_loading_draw_progress(
			&view->assets, &view->progress, &view->list, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_loading_draw_ready(
			&view->assets, &view->list, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_renderer_ui_frame(view->renderer, &view->list))
		status = mh_renderer_error(view->renderer, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_loading_confirmation_open(
			&view->confirmation, inputs, view->window, error_out);

	return status;
}

enum mh_error_code
mh_loading_view_wait(struct mh_loading_view *view,
	enum mh_loading_action *action, struct mh_error *error_out)
{
	if (view == NULL || action == NULL || view->renderer == NULL ||
		view->confirmation.input.backend == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"loading confirmation is not open");

	*action = mh_loading_confirmation_step(&view->confirmation);

	if (*action == MH_LOADING_WAIT &&
		!mh_renderer_ui_frame(view->renderer, &view->list))
		return mh_renderer_error(view->renderer, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

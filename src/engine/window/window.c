#include <stdlib.h>

#include "interfaces/image/pixels.h"
#include "interfaces/window/backend.h"

struct mh_window {
	/* The creating system stays borrowed until this window is destroyed. */
	const struct mh_window_system	*system;
	void				*context;
	int				diagnostic_titles;
};

static int
complete_ops(const struct mh_window_ops *ops)
{
	return ops != NULL && ops->acquire != NULL && ops->release != NULL &&
		ops->open != NULL && ops->destroy != NULL && ops->id != NULL;
}

static int
valid_system(const struct mh_window_system *system)
{
	return system != NULL && complete_ops(system->ops);
}

enum mh_error_code
mh_window_system_init(struct mh_window_system *system,
	const struct mh_window_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (system == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete window system");

	system->ops = ops;
	system->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_window_system_acquire(const struct mh_window_system *system,
	const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	if (!valid_system(system))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing window system");

	return system->ops->acquire(system->context, options, error_out);
}

void
mh_window_system_release(const struct mh_window_system *system)
{
	if (valid_system(system))
		system->ops->release(system->context);
}

int
mh_window_system_has_fullscreen(const struct mh_window_system *system)
{
	return valid_system(system) && system->ops->fullscreen != NULL;
}

enum mh_error_code
mh_window_system_modes(const struct mh_window_system *system,
	unsigned int limit, mh_display_mode_visit visit, void *context,
	struct mh_error *error_out)
{
	if (!valid_system(system) || limit == 0 || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid display mode query");

	if (system->ops->modes == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"display mode enumeration is unavailable");

	return system->ops->modes(
		system->context, limit, visit, context, error_out);
}

enum mh_error_code
mh_window_open(const struct mh_window_system *system, const char *title,
	unsigned int width, unsigned int height, unsigned int flags,
	struct mh_window **output, struct mh_error *error_out)
{
	struct mh_window *window;
	enum mh_error_code status;

	if (!valid_system(system) || title == NULL || output == NULL ||
		width == 0 || height == 0 ||
		(flags &
			~(MH_WINDOW_HIDDEN | MH_WINDOW_RESIZABLE |
				MH_WINDOW_FULLSCREEN |
				MH_WINDOW_DIAGNOSTIC_TITLES)) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid window arguments");

	if ((flags & MH_WINDOW_FULLSCREEN) && system->ops->fullscreen == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"fullscreen is unavailable");

	window = malloc(sizeof(*window));

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating window");

	window->system = system;
	window->diagnostic_titles = (flags & MH_WINDOW_DIAGNOSTIC_TITLES) != 0;
	window->context = NULL;
	status = system->ops->open(system->context, title, width, height,
		flags & ~MH_WINDOW_DIAGNOSTIC_TITLES, &window->context,
		error_out);

	if (status != MH_ERROR_NONE) {
		free(window);
		return status;
	}

	*output = window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_window_destroy(struct mh_window *window)
{
	if (window == NULL)
		return;

	window->system->ops->destroy(window->system->context, window->context);
	free(window);
}

unsigned long
mh_window_id(const struct mh_window *window)
{
	return window == NULL ? 0
			: window->system->ops->id(window->system->context,
					window->context);
}

enum mh_error_code
mh_window_set_visible(
	struct mh_window *window, int visible, struct mh_error *error_out)
{
	const struct mh_window_system *system;

	if (window == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing window");

	system = window->system;

	if ((visible && system->ops->show == NULL) ||
		(!visible && system->ops->hide == NULL))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window visibility is unavailable");

	return visible
		? system->ops->show(system->context, window->context, error_out)
		: system->ops->hide(
			system->context, window->context, error_out);
}

enum mh_error_code
mh_window_request_raise(struct mh_window *window, struct mh_error *error_out)
{
	if (window == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing window");

	if (window->system->ops->raise == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window raising is unavailable");

	return window->system->ops->raise(
		window->system->context, window->context, error_out);
}

enum mh_error_code
mh_window_set_title(
	struct mh_window *window, const char *title, struct mh_error *error_out)
{
	if (window == NULL || title == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid window title");

	if (window->system->ops->title == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window titles are unavailable");

	return window->system->ops->title(
		window->system->context, window->context, title, error_out);
}

enum mh_error_code
mh_window_set_icon(struct mh_window *window, const struct mh_image *image,
	struct mh_error *error_out)
{
	if (window == NULL || image == NULL || image->rgba == NULL ||
		image->width == 0 || image->height == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid window icon");

	if (window->system->ops->icon == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window icons are unavailable");

	return window->system->ops->icon(
		window->system->context, window->context, image, error_out);
}

enum mh_error_code
mh_window_set_fullscreen(
	struct mh_window *window, int fullscreen, struct mh_error *error_out)
{
	if (window == NULL || (fullscreen != 0 && fullscreen != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid fullscreen request");

	if (window->system->ops->fullscreen == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"fullscreen is unavailable");

	return window->system->ops->fullscreen(window->system->context,
		window->context, fullscreen, error_out);
}

enum mh_error_code
mh_window_get_size(struct mh_window *window, unsigned int *width,
	unsigned int *height, struct mh_error *error_out)
{
	if (window == NULL || width == NULL || height == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid window size query");

	if (window->system->ops->get_size == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window size query is unavailable");

	return window->system->ops->get_size(window->system->context,
		window->context, width, height, error_out);
}

enum mh_error_code
mh_window_set_size(struct mh_window *window, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	if (window == NULL || width == 0 || height == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid window size request");

	if (window->system->ops->set_size == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window resizing is unavailable");

	return window->system->ops->set_size(window->system->context,
		window->context, width, height, error_out);
}

void *
mh_window_backend_context(
	const struct mh_window *window, const struct mh_window_ops *ops)
{
	return window != NULL && window->system->ops == ops ? window->context
							: NULL;
}

int
mh_window_diagnostic_titles(const struct mh_window *window)
{
	return window != NULL && window->diagnostic_titles;
}

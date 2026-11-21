#include <stdlib.h>

#include "interfaces/render/canvas_backend.h"

struct mh_canvas {
	const struct mh_canvas_provider	*provider;
	void				*context;
};

static int
complete_ops(const struct mh_canvas_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->close != NULL &&
		ops->draw != NULL && ops->present != NULL;
}

enum mh_error_code
mh_canvas_provider_init(struct mh_canvas_provider *provider,
	const struct mh_canvas_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete canvas provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_canvas_open(const struct mh_canvas_provider *provider,
	struct mh_window *window, unsigned int width, unsigned int height,
	struct mh_canvas **output, struct mh_error *error_out)
{
	struct mh_canvas_layout layout;

	layout.width = layout.screen_width = width;
	layout.height = layout.screen_height = height;
	layout.x = layout.y = 0;

	return mh_canvas_open_layout(
		provider, window, &layout, output, error_out);
}

enum mh_error_code
mh_canvas_open_layout(const struct mh_canvas_provider *provider,
	struct mh_window *window, const struct mh_canvas_layout *layout,
	struct mh_canvas **output, struct mh_error *error_out)
{
	struct mh_canvas *canvas;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"canvas presentation is unavailable");

	if (!complete_ops(provider->ops) || window == NULL || output == NULL ||
		layout == NULL || layout->width == 0 || layout->height == 0 ||
		layout->width > (size_t)-1 / 4 / layout->height ||
		layout->x > layout->screen_width ||
		layout->y > layout->screen_height ||
		layout->width > layout->screen_width - layout->x ||
		layout->height > layout->screen_height - layout->y)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid canvas arguments");

	canvas = malloc(sizeof(*canvas));

	if (canvas == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating canvas");

	canvas->provider = provider;
	canvas->context = NULL;
	status = provider->ops->open(
		provider->context, window, layout, &canvas->context, error_out);

	if (status != MH_ERROR_NONE) {
		free(canvas);
		return status;
	}

	*output = canvas;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_canvas_destroy(struct mh_canvas *canvas)
{
	if (canvas == NULL)
		return;

	canvas->provider->ops->close(
		canvas->provider->context, canvas->context);
	free(canvas);
}

enum mh_error_code
mh_canvas_draw(struct mh_canvas *canvas, const unsigned char *pixels,
	struct mh_error *error_out)
{
	if (canvas == NULL || pixels == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing canvas pixels");

	return canvas->provider->ops->draw(
		canvas->provider->context, canvas->context, pixels, error_out);
}

enum mh_error_code
mh_canvas_present(struct mh_canvas *canvas, struct mh_error *error_out)
{
	if (canvas == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing canvas");

	return canvas->provider->ops->present(
		canvas->provider->context, canvas->context, error_out);
}

enum mh_error_code
mh_canvas_read(struct mh_canvas *canvas, struct mh_image *output,
	struct mh_error *error_out)
{
	if (canvas == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing canvas readback arguments");

	if (canvas->provider->ops->read == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"canvas readback is unavailable");

	return canvas->provider->ops->read(
		canvas->provider->context, canvas->context, output, error_out);
}

enum mh_error_code
mh_canvas_set_vsync(
	struct mh_canvas *canvas, int enabled, struct mh_error *error_out)
{
	if (canvas == NULL || (enabled != 0 && enabled != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid canvas VSync request");

	if (canvas->provider->ops->set_vsync == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"canvas VSync control is unavailable");

	return canvas->provider->ops->set_vsync(
		canvas->provider->context, canvas->context, enabled, error_out);
}

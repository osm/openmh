#include <SDL3/SDL.h>
#include <stdlib.h>

#include "backends/render/webgl_context.h"
#include "backends/render/webgl_provider.h"
#include "backends/window/sdl.h"

static const char context_property[] = "openmotor.webgl.context";

struct window_context {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	handle;
	const struct mh_webgl_context	*acquisition;
	int				generation;
};

/* webgl_context.js observes browser events and owns SDK bool callbacks. */
int	mh_webgl_context_watch(int handle);
int	mh_webgl_context_generation(int handle);
void	mh_webgl_context_unwatch(int handle);

static void SDLCALL
destroy_context(void *userdata, void *value)
{
	struct window_context *context;

	(void)userdata;
	context = value;
	mh_webgl_context_unwatch(context->handle);
	emscripten_webgl_destroy_context(context->handle);
	free(context);
}

static enum mh_error_code
create_context(SDL_PropertiesID properties, const char *selector,
	struct window_context **output, struct mh_error *error_out)
{
	EmscriptenWebGLContextAttributes attributes;
	struct window_context *context;

	context = calloc(1, sizeof(*context));

	if (context == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL context owner");

	emscripten_webgl_init_context_attributes(&attributes);
	attributes.majorVersion = 2;
	attributes.alpha = EM_FALSE;
	attributes.depth = EM_FALSE;
	attributes.stencil = EM_FALSE;
	attributes.antialias = EM_FALSE;
	context->handle =
		emscripten_webgl_create_context(selector, &attributes);

	if (context->handle <= 0) {
		free(context);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot create WebGL 2 context");
	}

	if (!mh_webgl_context_watch(context->handle)) {
		destroy_context(NULL, context);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot observe WebGL context restoration");
	}

	/*
	 * SDL invokes cleanup on failure too, so do not destroy context twice.
	 */
	if (!SDL_SetPointerPropertyWithCleanup(properties, context_property,
		context, destroy_context, NULL))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot retain WebGL context on window");

	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_context_open(struct mh_webgl_context *output, struct mh_window *window,
	struct mh_error *error_out)
{
	SDL_Window *native;
	SDL_PropertiesID properties;
	struct window_context *context;
	enum mh_error_code status;
	const char *selector;

	if (output == NULL || window == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL context output or window");

	if (output->handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL context is already open");

	native = mh_sdl_window(window);

	if (native == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"WebGL presentation requires an SDL browser window");

	properties = SDL_GetWindowProperties(native);
	selector = SDL_GetStringProperty(
		properties, SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, NULL);

	if (selector == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window has no browser canvas");

	context = SDL_GetPointerProperty(properties, context_property, NULL);

	if (SDL_GetRenderer(native) != NULL ||
		(context != NULL && context->acquisition != NULL))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"browser window already has a presentation owner");

	if (context == NULL) {
		status = create_context(
			properties, selector, &context, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (emscripten_is_webgl_context_lost(context->handle))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "WebGL context was lost");

	if (emscripten_webgl_make_context_current(context->handle) !=
		EMSCRIPTEN_RESULT_SUCCESS)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot acquire WebGL 2 context");

	context->acquisition = output;
	context->generation = mh_webgl_context_generation(context->handle);
	output->handle = context->handle;
	output->window = window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_context_current(
	const struct mh_webgl_context *context, struct mh_error *error_out)
{
	struct window_context *owner;
	enum mh_error_code status;
	SDL_Window *window;
	int generation;

	if (context == NULL || context->handle <= 0)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "WebGL context is not open");

	if (emscripten_is_webgl_context_lost(context->handle))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "WebGL context was lost");

	if (emscripten_webgl_make_context_current(context->handle) !=
		EMSCRIPTEN_RESULT_SUCCESS)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot select WebGL context");

	window = mh_sdl_window(context->window);
	owner = window == NULL
		? NULL
		: SDL_GetPointerProperty(SDL_GetWindowProperties(window),
			context_property, NULL);

	if (owner == NULL || owner->acquisition != context)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL context has no presentation owner");

	generation = mh_webgl_context_generation(context->handle);

	if (generation < 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL context restoration observer is unavailable");

	if (owner->generation != generation) {
		if (context->restore == NULL)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"WebGL presentation owner cannot restore its resources");

		status = context->restore(context->restore_context, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		owner->generation = generation;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_context_size(const struct mh_webgl_context *context,
	unsigned int *width, unsigned int *height, struct mh_error *error_out)
{
	int w, h;

	if (context == NULL || context->handle <= 0 || width == NULL ||
		height == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL drawable-size request");

	if (emscripten_webgl_get_drawing_buffer_size(context->handle, &w, &h) !=
			EMSCRIPTEN_RESULT_SUCCESS ||
		w <= 0 || h <= 0)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebGL drawing buffer is unavailable");

	*width = (unsigned int)w;
	*height = (unsigned int)h;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_context_close(struct mh_webgl_context *context)
{
	SDL_Window *window;
	struct window_context *owner;

	if (context == NULL || context->handle <= 0)
		return;

	window = mh_sdl_window(context->window);

	if (window != NULL) {
		owner = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
			context_property, NULL);

		if (owner != NULL)
			owner->acquisition = NULL;
	}

	/*
	 * Destroying an Emscripten context also removes canvas input handlers.
	 * Keep it until the SDL window dies, across loading/menu/race handoffs.
	 */
	context->handle = 0;
	context->window = NULL;
	context->restore = NULL;
	context->restore_context = NULL;
}

enum mh_error_code
mh_webgl_restore_windows(struct mh_error *error_out)
{
	SDL_Window **windows;
	struct window_context *owner;
	enum mh_error_code status;
	int i, count;

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	windows = SDL_GetWindows(&count);

	if (windows == NULL)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	status = MH_ERROR_NONE;

	for (i = 0; status == MH_ERROR_NONE && i < count; i++) {
		owner = SDL_GetPointerProperty(
			SDL_GetWindowProperties(windows[i]), context_property,
			NULL);

		if (owner != NULL && owner->acquisition != NULL)
			status = mh_webgl_context_current(
				owner->acquisition, error_out);
	}

	SDL_free(windows);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

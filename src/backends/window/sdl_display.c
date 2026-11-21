#include "backends/window/sdl.h"

/* Display probing supplies sizes only; selection and fallback are portable. */
enum mh_error_code
mh_sdl_display_modes(void *context, unsigned int limit,
	mh_display_mode_visit visit, void *visitor, struct mh_error *error_out)
{
	SDL_DisplayID display;
	SDL_DisplayMode **modes;
	const SDL_DisplayMode *desktop;
	unsigned int visited;
	int count, i;

	(void)context;
	display = SDL_GetPrimaryDisplay();

	if (display == 0)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	visited = 0;
	desktop = SDL_GetDesktopDisplayMode(display);

	if (desktop != NULL && desktop->w > 0 && desktop->h > 0) {
		visited++;

		if (!visit(visitor, (unsigned int)desktop->w,
			(unsigned int)desktop->h) ||
			visited == limit)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	count = 0;
	modes = SDL_GetFullscreenDisplayModes(display, &count);

	if (modes == NULL)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	for (i = 0; i < count && (unsigned int)i < limit && visited < limit &&
		modes[i] != NULL;
		i++) {
		if (modes[i]->w <= 0 || modes[i]->h <= 0)
			continue;

		visited++;

		if (!visit(visitor, (unsigned int)modes[i]->w,
			(unsigned int)modes[i]->h))
			break;
	}

	SDL_free(modes);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

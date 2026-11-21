#ifndef MH_BACKENDS_RENDER_SDL_CANVAS_PROVIDER_H
#define MH_BACKENDS_RENDER_SDL_CANVAS_PROVIDER_H

#include "interfaces/render/canvas.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/capture.h"

const struct mh_canvas_provider	*mh_sdl_canvas_provider(void);

/* Native BMP sink; independent of scene rendering. */
const struct mh_image_capture	*mh_sdl_image_capture(void);

/* Borrows packed RGBA bytes for this call; the caller retains ownership. */
enum mh_error_code	mh_image_save(const unsigned char *pixels,
	unsigned int width, unsigned int height, const char *path,
	struct mh_error *error_out);

#endif

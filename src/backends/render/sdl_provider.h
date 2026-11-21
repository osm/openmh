#ifndef MH_BACKENDS_RENDER_SDL_PROVIDER_H
#define MH_BACKENDS_RENDER_SDL_PROVIDER_H

#include "interfaces/render/service.h"

/* Native composition; initialize on the host event thread, no device I/O. */
const struct mh_render_service	*mh_sdl_render_service(void);

#endif

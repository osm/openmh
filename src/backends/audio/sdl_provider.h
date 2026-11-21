#ifndef MH_BACKENDS_AUDIO_SDL_PROVIDER_H
#define MH_BACKENDS_AUDIO_SDL_PROVIDER_H

#include "interfaces/audio/backend.h"

/* Process-lifetime descriptor; each open owns its stream and worker. */
const struct mh_audio_provider	*mh_sdl_audio_provider(void);

#endif

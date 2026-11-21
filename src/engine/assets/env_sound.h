#ifndef MH_ASSETS_ENV_SOUND_H
#define MH_ASSETS_ENV_SOUND_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/audio/environment.h"

/*
 * Decoded/plain ASCII ESD. Fixed positions and object class references are
 * retained separately. Bounded input and output unchanged on failure.
 */
enum mh_error_code	mh_env_sounds_esd_decode(const unsigned char *text,
	size_t size, struct mh_env_sounds *output, struct mh_error *error_out);

#endif

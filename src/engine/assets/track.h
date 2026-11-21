#ifndef MH_ASSETS_TRACK_H
#define MH_ASSETS_TRACK_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/catalog/track.h"

/* Decoded ASCII input. Unknown keys are ignored; errors preserve output. */
enum mh_error_code	mh_track_config_trk_decode(const unsigned char *text,
	size_t size, struct mh_track *track, struct mh_error *error_out);

#endif

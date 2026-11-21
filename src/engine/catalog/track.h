#ifndef MH_ENGINE_CATALOG_TRACK_H
#define MH_ENGINE_CATALOG_TRACK_H

#include "interfaces/catalog/track.h"

/* Material flags use the original separate prefix comparison. */
int	mh_track_zero_transparent(const struct mh_track *track,
	const char *name);

/* Texture archive/format selection requires full name equality. */
int	mh_track_texture_cutout(const struct mh_track *track, const char *name);

#endif

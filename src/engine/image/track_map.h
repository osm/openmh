#ifndef MH_IMAGE_TRACK_MAP_H
#define MH_IMAGE_TRACK_MAP_H

#include "engine/image/load.h"
#include "interfaces/catalog/track.h"

/* Owned RGBA map, shared by the HUD and selectors. An absent map is empty. */
enum mh_error_code	mh_track_map_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_image *output,
	struct mh_error *error_out);

#endif

#ifndef MH_POWERVR_LAMP_ATLAS_H
#define MH_POWERVR_LAMP_ATLAS_H

#include "engine/assets/image.h"
#include "engine/assets/name_images.h"
#include "interfaces/image/pixels.h"

#define MH_POWERVR_LAMP_ATLAS_SIZE 256
#define MH_POWERVR_NAME_COLUMNS 2
#define MH_POWERVR_NAME_CELL_WIDTH                                             \
	(MH_POWERVR_LAMP_ATLAS_SIZE / MH_POWERVR_NAME_COLUMNS)

/*
 * Shared halo, flare and shadow atlas, without player-name labels. Sources are
 * the 128px glow and 64px flare indices; both use the glow palette. The
 * original replaces their RGB with white and derives alpha from brightness,
 * then inserts the three-row shadow ramp. Caller supplies disjoint 256x256
 * RGBA8 output. No allocation or mutation of sources. Invalid input leaves
 * output unchanged. Texture upload applies ARGB4444 precision afterward; these
 * are the original preparation bytes.
 */
enum mh_error_code	mh_powervr_lamp_atlas_prepare(
	const struct mh_indexed_image *glow,
	const struct mh_indexed_image *flare, struct mh_image *output,
	struct mh_error *error_out);

/* Replace the lower-half label slots, preserving upper-half effects. */
enum mh_error_code	mh_powervr_lamp_atlas_names(
	const struct mh_name_images *names,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_image *atlas, struct mh_name_label labels[MH_NAME_LABELS_MAX],
	struct mh_error *error_out);

#endif

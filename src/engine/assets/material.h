#ifndef MH_ASSETS_MATERIAL_H
#define MH_ASSETS_MATERIAL_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/material/material.h"

/*
 * Decoded Material.mat plus plain-text or TBL05 track.SRF. SRF names occupy the
 * leading slots in COLLISION order; unused MAT definitions are appended.
 * Missing definitions/settings get the asphalt-like defaults. Missing sounds
 * are silent; missing SlideType/effect IDs are zero. Unknown keys are ignored.
 * Rejects duplicate names/settings, invalid ASCII, counts, values and names
 * over 31 bytes. Limit: 64 combined records, 26 SRF slots. Wrapped SRF text
 * uses a temporary allocation, freed before return; plain-text input needs
 * none. Output unchanged on failure.
 */
enum mh_error_code	mh_materials_mat_srf_decode(
	const unsigned char *definitions, size_t size,
	const unsigned char *surfaces, size_t surface_size,
	struct mh_materials *output, struct mh_error *error_out);

/*
 * Read one MAT definition list (surface_names=0) or SRF name list (1). SRF
 * entries retain the defaults until combined with a MAT. Same validation/atomic
 * output contract; no allocation.
 */
enum mh_error_code	mh_materials_mat_srf_decode_list(
	const unsigned char *text, size_t size, int surface_names,
	struct mh_materials *output, struct mh_error *error_out);

#endif

#ifndef MH_ASSETS_NAME_ATLAS_H
#define MH_ASSETS_NAME_ATLAS_H

#include "engine/core/error.h"
#include "engine/render/name_label.h"
#include "interfaces/catalog/profile.h"
#include "interfaces/font/glyphs.h"

struct mh_name_atlas {
	struct mh_image		image;
	struct mh_name_label	labels[MH_NAME_LABELS_MAX];
	unsigned int		count, active_mask;
};

/*
 * Original nickname bitmaps and hardware packing. Decode the selected original
 * FNT as byte spans before calling. This also preserves FONT2's byte widths
 * when the original switches to indexed mode. Rows start in the lower half when
 * the roster fits; larger rosters also use the upper half. The original shares
 * those upper rows with halos. Output owns image.rgba; unchanged on error. No
 * dependency on inputs remains.
 */
enum mh_error_code	mh_name_atlas_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_atlas *output,
	struct mh_error *error_out);
void			mh_name_atlas_free(struct mh_name_atlas *atlas);

#endif

#ifndef MH_ASSETS_NAME_IMAGES_H
#define MH_ASSETS_NAME_IMAGES_H

#include "engine/render/name_label.h"
#include "interfaces/catalog/profile.h"
#include "interfaces/font/glyphs.h"

struct mh_name_images {
	struct mh_indexed_image	images[MH_NAME_LABELS_MAX];
	unsigned int		count, active_mask;
};

/*
 * Original per-driver byte images, without hardware atlas packing. Each owned
 * image keeps its full height and uses black and the driver color. Decode the
 * selected original font first. Errors preserve output; success transfers the
 * images and retains no input pointers.
 */
enum mh_error_code	mh_name_images_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_images *output,
	struct mh_error *error_out);
void			mh_name_images_free(struct mh_name_images *names);

#endif

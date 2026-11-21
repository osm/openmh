#ifndef MH_ASSETS_NAME_MESH_H
#define MH_ASSETS_NAME_MESH_H

#include "engine/assets/name_atlas.h"
#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "interfaces/image/pixels.h"

enum {
	MH_NAME_QUAD_VERTICES = 6
};

struct mh_name_mesh {
	enum mh_render_profile	profile;
	struct mh_mesh		mesh;
	struct mh_name_label	labels[MH_NAME_LABELS_MAX];
	unsigned int		count, active_mask, visible_mask;
};

enum mh_error_code	mh_name_mesh_allocate(struct mh_name_mesh *result,
	struct mh_error *error_out);

/*
 * Owns the original font-derived atlas and independently submitted
 * quads. Draw/vertex slots are in reverse roster order: entry E uses draw
 * count-1-E and its six vertices. Hidden slots stay degenerate,
 * so effect interleaving never depends on the previous visible-name count.
 */
enum mh_error_code	mh_name_mesh_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_name_mesh *output,
	struct mh_error *error_out);
void			mh_name_mesh_free(struct mh_name_mesh *names);

/*
 * Borrow the shared PowerVR page and stamp original indexed glyphs into it.
 * Call before GPU creation; failure leaves the page and output unchanged.
 */
enum mh_error_code	mh_powervr_name_mesh_create(const struct mh_font *font,
	const struct mh_profile *profiles, unsigned int count,
	unsigned int active_mask, struct mh_atlas *atlas,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_name_mesh *output, struct mh_error *error_out);

#endif

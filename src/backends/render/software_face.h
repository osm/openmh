#ifndef MH_SOFTWARE_FACE_H
#define MH_SOFTWARE_FACE_H

#include "backends/render/software_near_clip.h"
#include "backends/render/software_raster.h"
#include "interfaces/render/renderer.h"

/* Descriptor flags in addition to the shared mh_near_flags. */
enum mh_software_face_flags {
	MH_SOFTWARE_FACE_PERSPECTIVE = 0x10,
	MH_SOFTWARE_FACE_ADDITIVE = 0x40,
	MH_SOFTWARE_FACE_CUTOUT = 0x400,
	MH_SOFTWARE_FACE_OUTLINE = 0x1000
};

struct mh_software_material {
	const struct mh_software_texture	*texture;
	float					fog_color[3], fog;
	enum mh_texture_mapping			mapping;
	unsigned int				perspective_shift;
};

/*
 * Prepare a projected face, including mh_software_clip_near output. Quads
 * dispatch 023 before 012; perspective policy is evaluated per triangle.
 * Textured faces shade from original corner zero, even with vertex RGB flags.
 * Smooth RGB saturates here, after clipping; flat/textured light stays
 * unbounded by the byte range until fog/palette shading. Texture/palette
 * storage remains borrowed. Check every prepared triangle with
 * mh_software_triangle_check before drawing a frame; this operation validates
 * descriptor shape/material policy, not raster coordinates or texture samples.
 * Errors leave output/count unchanged. Success writes one or two triangles.
 */
enum mh_error_code	mh_software_face_prepare(
	const struct mh_software_clipped_face *face,
	const struct mh_software_material *material,
	struct mh_software_triangle output[2], unsigned int *count,
	struct mh_error *error_out);

#endif

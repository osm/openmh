#ifndef MH_SCENE_FACE_INTERNAL_H
#define MH_SCENE_FACE_INTERNAL_H

#include "engine/render/scene_face.h"

/* Shared clipping mechanics; each visual profile owns its material policy. */
enum mh_scene_face_flag {
	MH_FACE_RAW_COLOR = 1,	     /* Signed RGB, packed after clipping. */
	MH_FACE_DISTANT_TEXTURE = 2, /* Retain textures on distant faces. */
	MH_FACE_CONTEXT_FOG = 4, /* Fog is separate from visibility bytes. */
	MH_FACE_PACK_DISTANT = 8 /* Distant faces retain packed RGB too. */
};

/* Callers supply a valid policy and both callbacks. */
struct mh_scene_face_policy {
	unsigned int	flags;
	int		(*distant)(const float *, unsigned int);
	int		(*background)(float);
};

enum mh_error_code	mh_scene_face_prepare(
	const struct mh_projection *projection, enum mh_scene_pass material,
	unsigned int count, int smooth, const struct mh_scene_corner input[4],
	struct mh_scene_face output[2], unsigned int *output_count,
	const struct mh_scene_face_policy *policy, int fog,
	struct mh_error *error_out);

#endif

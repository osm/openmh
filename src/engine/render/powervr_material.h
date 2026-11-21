#ifndef MH_POWERVR_MATERIAL_H
#define MH_POWERVR_MATERIAL_H

#include "engine/render/material_lists.h"
#include "engine/render/mesh.h"
#include "engine/render/scene_effect.h"
#include "engine/render/scene_face.h"

/* Original distant-face color/opacity packing, before GPU submission. */
void	mh_powervr_face_color_prepare(struct mh_scene_face *face, int smooth);
void	mh_powervr_vertex_color_prepare(struct mh_vertex *vertex,
	const struct mh_scene_face *face, unsigned int corner,
	enum mh_scene_effect effect, size_t list);

/* Writes quad colors in order; a failed pack may leave an updated prefix. */
enum mh_error_code	mh_powervr_effect_colors_prepare(
	const struct mh_mesh *mesh, struct mh_vertex *vertices,
	enum mh_scene_effect effect, struct mh_error *error_out);

#endif

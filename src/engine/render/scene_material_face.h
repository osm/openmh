#ifndef MH_SCENE_MATERIAL_FACE_H
#define MH_SCENE_MATERIAL_FACE_H

#include "engine/render/material_lists.h"
#include "engine/render/mesh.h"
#include "engine/render/scene_effect.h"
#include "engine/render/scene_face.h"
#include "interfaces/render/glenz.h"

/*
 * Borrowed mesh and latest successfully uploaded CPU attributes. PowerVR's
 * ordinary texture coordinates need both source and padded texture dimensions.
 * These describe one selected draw, not backend GPU resources.
 */
struct mh_material_face_source {
	const struct mh_mesh			*mesh;
	const float	*positions, *colors, *material_rgb;
	const struct mh_glenz_coordinates	*glenz_coordinates;
	const struct mh_image			*image;
	unsigned int				texture_width, texture_height;
	enum mh_scene_effect			effect;
	int					powervr, glenz, fog_enabled;
};

struct mh_material_faces {
	struct mh_scene_face		faces[2];
	struct mh_glenz_coordinates	coordinates[4];
	size_t				lists[2];
	unsigned int			count;
	int				glenz, near_clipped;
};

/*
 * Project, clip, shade and classify one original triangle or quad. Success
 * publishes zero, one or two faces; failure leaves output unchanged. Submit
 * faces in reverse index order when prepending to the original material lists.
 */
enum mh_error_code	mh_material_face_prepare(
	const struct mh_material_face_source *source, size_t draw, size_t first,
	size_t count, unsigned int type, const float camera[16],
	const struct mh_projection *projection, int reflections,
	struct mh_material_faces *output, struct mh_error *error_out);

/*
 * Expand a successfully prepared face into 3 or 6 vertices, returned as count.
 * Preserve the original 0-2 quad diagonal and first-corner provoking order.
 * Backends with last-corner flat shading must rotate each triangle on upload.
 */
unsigned int	mh_material_face_vertices(
	const struct mh_material_face_source *source,
	const struct mh_material_faces *prepared, unsigned int face,
	const struct mh_projection *projection, struct mh_vertex vertices[6],
	struct mh_glenz_coordinates coordinates[6]);

#endif

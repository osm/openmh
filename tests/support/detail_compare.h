#ifndef MH_TEST_DETAIL_COMPARE_H
#define MH_TEST_DETAIL_COMPARE_H

#include <string.h>

#include "engine/core/error.h"
#include "engine/render/mesh.h"

static int
same_image(const struct mh_image *a, const struct mh_image *b)
{
	return a->width == b->width && a->height == b->height &&
		memcmp(a->rgba, b->rgba, (size_t)a->width * a->height * 4) == 0;
}

static enum mh_error_code
compare(const struct mh_mesh *packed, const struct mh_mesh *single,
	struct mh_error *error_out)
{
	struct mh_mesh_lod range;
	struct mh_draw draw;
	const struct mh_draw *other_draw;
	struct mh_mesh_face face;
	const struct mh_mesh_face *other_face;
	struct mh_mesh_primitive primitive;
	const struct mh_mesh_primitive *other_primitive;
	size_t i, texture;
	enum mh_error_code status;

	status = mh_mesh_active_lod(packed, &range, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (range.vertex_count != single->vertex_count ||
		range.draw_count != single->draw_count ||
		range.face_count != single->face_count ||
		range.primitive_count != single->primitive_count ||
		range.halo_count != single->halo_count)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"detail slice counts differ");

	if (memcmp(packed->vertices + range.vertex_first, single->vertices,
		single->vertex_count * sizeof(*single->vertices)) != 0 ||
		(single->halo_count != 0 &&
			memcmp(packed->halos + range.halo_first, single->halos,
				single->halo_count * sizeof(*single->halos)) !=
				0))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"detail geometry or local halos differ");

	for (i = 0; i < single->draw_count; i++) {
		draw = packed->draws[range.draw_first + i];
		texture = draw.texture;
		draw.first -= range.vertex_first;
		draw.texture = single->draws[i].texture;
		other_draw = &single->draws[i];

		if (draw.first != other_draw->first ||
			draw.count != other_draw->count ||
			draw.environment != other_draw->environment ||
			draw.prelit != other_draw->prelit ||
			draw.alpha_test != other_draw->alpha_test ||
			strcmp(draw.texture_name, other_draw->texture_name) !=
				0 ||
			!same_image(&packed->textures[texture],
				&single->textures[draw.texture]))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"detail materials or image pixels differ");
	}

	for (i = 0; i < single->face_count; i++) {
		face = packed->faces[range.face_first + i];
		face.first -= range.vertex_first;
		other_face = &single->faces[i];

		if (face.first != other_face->first ||
			face.count != other_face->count ||
			face.type != other_face->type ||
			face.flags != other_face->flags ||
			memcmp(face.color, other_face->color,
				sizeof(face.color)) != 0 ||
			memcmp(face.plane, other_face->plane,
				sizeof(face.plane)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"detail lighting face differs");
	}

	for (i = 0; i < single->primitive_count; i++) {
		primitive = packed->primitives[range.primitive_first + i];
		primitive.first -= range.vertex_first;
		primitive.draw -= range.draw_first;
		other_primitive = &single->primitives[i];

		if (primitive.first != other_primitive->first ||
			primitive.draw != other_primitive->draw ||
			primitive.count != other_primitive->count ||
			primitive.type != other_primitive->type ||
			primitive.flags != other_primitive->flags)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"detail material primitive differs");
	}

	if (!same_image(&packed->textures[packed->environment_texture],
		&single->textures[single->environment_texture]))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"detail reflection image differs");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#endif

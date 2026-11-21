#include "engine/ui/retro.h"

enum {
	VIEWPORT_LIMIT = 16384,
	DEPTH_RANGE = 65536,
	COORDINATE_LIMIT = 65536
};

enum mh_error_code
mh_retro_draw_validate(
	const struct mh_retro_draw *draw, struct mh_error *error_out)
{
	const struct mh_retro_draw_vertex *vertex;
	unsigned int i, j;

	if (draw == NULL || draw->text_end < MH_RETRO_BACKDROP_VERTICES ||
		draw->text_end > draw->count ||
		draw->count > MH_RETRO_MAX_VERTICES || draw->count % 3 != 0 ||
		draw->text_end % MH_RETRO_QUAD_VERTICES != 0 ||
		draw->width < 1 || draw->width > VIEWPORT_LIMIT ||
		draw->height < 1 || draw->height > VIEWPORT_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid retro GPU frame");

	for (i = 0; i < draw->count; i++) {
		vertex = &draw->vertices[i];

		if (!(vertex->vertex.depth >= 0 &&
			vertex->vertex.depth <= DEPTH_RANGE) ||
			!(vertex->vertex.alpha >= 0 &&
				vertex->vertex.alpha <= 255))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro GPU depth/alpha");

		for (j = 0; j < 2; j++)
			if (!(vertex->vertex.xy[j] >= -COORDINATE_LIMIT &&
				vertex->vertex.xy[j] <= COORDINATE_LIMIT) ||
				!(vertex->uv[j] >= -.01f &&
					vertex->uv[j] <= 1.01f))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro GPU coordinate");

		for (j = 0; j < 3; j++)
			if (!(vertex->vertex.rgb[j] >= 0 &&
				vertex->vertex.rgb[j] <= 255))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro GPU color");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

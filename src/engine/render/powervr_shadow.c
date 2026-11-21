#include <string.h>

#include "engine/render/powervr_effect.h"
#include "engine/render/powervr_shadow.h"

#define TRANSMISSION_MAX 255

/* Reject whole shadow quads at either depth plane. */
enum mh_error_code
mh_powervr_shadow_prepare(const struct mh_projection *projection,
	const struct mh_scene_corner input[4], struct mh_scene_face *face,
	unsigned int *used, struct mh_error *error_out)
{
	struct mh_powervr_shadow_corner shadow;
	struct mh_scene_vertex *corner;
	const struct mh_scene_corner *source;
	enum mh_error_code status;
	unsigned int index, shared;
	float transmission;

	*used = 0;
	shared = MH_CLIP_VIEWPORT;

	for (index = 0; index < 4; index++) {
		if (input[index].point.outcode & (MH_CLIP_NEAR | MH_CLIP_FAR))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		shared &= input[index].point.outcode;
	}

	if (shared != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(face, 0, sizeof(*face));
	face->count = 4;

	for (index = 0; index < 4; index++) {
		source = &input[index];
		transmission = source->rgb[0];

		if (!(transmission >= 0 && transmission <= TRANSMISSION_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid shadow transmission");

		status = mh_powervr_shadow_corner(source->point.value[3],
			(unsigned char)(transmission + .5f), &shadow,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		corner = &face->corners[index];
		memcpy(corner->xy, source->point.value, sizeof(corner->xy));

		/* Keep clip Z inside the lens; PowerVR tests reciprocal W. */
		corner->depth =
			projection->clip.near_distance * source->point.value[3];
		corner->reciprocal_w = shadow.reciprocal_w;
		corner->uv[0] = shadow.uv[0] / shadow.reciprocal_w;
		corner->uv[1] = shadow.uv[1] / shadow.reciprocal_w;
	}

	*used = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

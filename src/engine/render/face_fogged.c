#include "engine/render/face_fogged.h"
#include "engine/render/fog.h"
#include "engine/render/scene_face_internal.h"

enum mh_error_code
mh_fogged_face_prepare(const struct mh_projection *projection,
	enum mh_scene_pass material, unsigned int count, int smooth,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, struct mh_error *error_out)
{
	static const struct mh_scene_face_policy policy = {
		0, mh_fog_distant, mh_fog_background};

	return mh_scene_face_prepare(projection, material, count, smooth, input,
		output, output_count, &policy, 1, error_out);
}

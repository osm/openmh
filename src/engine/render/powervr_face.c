#include "engine/render/fog.h"
#include "engine/render/powervr_face.h"
#include "engine/render/scene_face_internal.h"

#define COLOR_BYTE_MAX 255
#define POWERVR_DISTANT_START .5f
#define POWERVR_FADE_START .6f
#define POWERVR_FADE_SCALE 3.5L

static int
powervr_distant(const float depths[4], unsigned int count)
{
	unsigned int corner;

	for (corner = 0; corner < count; corner++)
		if (depths[corner] <= POWERVR_DISTANT_START)
			return 0;

	return 1;
}

static int
powervr_background(float depth)
{
	float value;

	value = (float)((long double)depth - POWERVR_FADE_START);

	if (value < 0)
		value = 0;

	value = (float)((long double)value * POWERVR_FADE_SCALE);

	if (value > 1)
		value = 1;

	return (unsigned int)((long double)value * COLOR_BYTE_MAX);
}

enum mh_error_code
mh_powervr_face_prepare(const struct mh_projection *projection,
	enum mh_scene_pass material, unsigned int count, int smooth,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, struct mh_error *error_out)
{
	static const struct mh_scene_face_policy policy = {MH_FACE_RAW_COLOR |
			MH_FACE_DISTANT_TEXTURE | MH_FACE_CONTEXT_FOG |
			MH_FACE_PACK_DISTANT,
		powervr_distant, powervr_background};

	if (smooth != 0 && smooth != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR scene shading");

	return mh_scene_face_prepare(projection, material, count,
		material == MH_SCENE_ENVIRONMENT ? 1 : smooth, input, output,
		output_count, &policy, 1, error_out);
}

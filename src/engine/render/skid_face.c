#include <string.h>

#include "engine/render/fog.h"
#include "engine/render/scene_face_internal.h"
#include "engine/render/skid_face.h"

#define SKID_FADE_BASE 180
#define SKID_FADE_SCALE 200
#define SKID_NEAR_SHADE 155
#define SKID_DEPTH_OFFSET (-.2)
/* Original packed binary32 0x3f7fff58, used before depth reversal. */
#define SKID_NEAR_DEPTH .999989986419677734375f
#define COLOR_MAX 255

/* Projected caches through color/depth and near submission. */
enum mh_error_code
mh_skid_face_prepare(const struct mh_projection *projection,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, struct mh_error *error_out)
{
	static const struct mh_scene_face_policy policy = {
		MH_FACE_RAW_COLOR, mh_fog_distant, mh_fog_background};

	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2];
	const struct mh_near_projection *clip;
	const struct mh_projected_point *point;
	float shade, depth;
	unsigned int corner, channel, used;
	int near_clipped;
	enum mh_error_code status;

	if (projection == NULL || input == NULL || output == NULL ||
		output_count == NULL ||
		!(projection->far_distance > projection->clip.near_distance &&
			projection->far_distance <= 1e9f))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid skid polygon");

	clip = &projection->clip;
	near_clipped = 0;

	for (corner = 0; corner < 4; corner++)
		near_clipped |=
			(input[corner].point.outcode & MH_CLIP_NEAR) != 0;

	memset(corners, 0, sizeof(corners));

	for (corner = 0; corner < 4; corner++) {
		point = &input[corner].point;
		corners[corner].point = *point;
		shade = near_clipped ? SKID_NEAR_SHADE
				: (float)((long double)point->value[2] *
						SKID_FADE_SCALE +
					SKID_FADE_BASE);

		if (shade > COLOR_MAX)
			shade = COLOR_MAX;

		for (channel = 0; channel < 3; channel++)
			corners[corner].rgb[channel] = shade;
	}

	/* Skid ribbons disable fog; only shared clipping and packing apply. */
	status = mh_scene_face_prepare(projection, MH_SCENE_SOLID, 4, 1,
		corners, faces, &used, &policy, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* The near-split branch retains the shared splitter's depth. */
	if (used != 0 && !near_clipped)
		for (corner = 0; corner < 4; corner++) {
			point = &input[corner].point;
			depth = (float)((long double)point->value[2] *
					projection->far_distance +
				SKID_DEPTH_OFFSET);
			faces[0].corners[corner].depth =
				depth > clip->near_distance
				? (float)((long double)clip->near_distance /
					depth)
				: SKID_NEAR_DEPTH;
		}

	if (used != 0)
		memcpy(output, faces, used * sizeof(*output));

	*output_count = used;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/render/lod.h"

#define MAX_LEVELS 4
#define IGNITION_LEVEL 2
#define SUPERCARS_LEVEL 3

#define POSITION_LIMIT 1e6f
#define FOCAL_MIN .5f
#define FOCAL_MAX 10
#define VIEWPORT_LIMIT 16384
#define REFERENCE_WIDTH 512
#define DETAIL_DISTANCE 20

/* Preserve two refinements of the C89 binary64 sqrt seed. */
static long double
refined_sqrt(long double value)
{
	long double result;

	if (value == 0)
		return 0;

	result = sqrt((double)value);
	result = (result + value / result) * .5L;

	return (result + value / result) * .5L;
}

/* Compare in the software scene's binary32 precision. */
static float
software_distance(const float delta[3])
{
	float x, y, z, sum;

	x = (float)((double)delta[0] * delta[0]);
	y = (float)((double)delta[1] * delta[1]);
	z = (float)((double)delta[2] * delta[2]);
	sum = (float)((double)y + x);
	sum = (float)((double)sum + z);

	return (float)sqrt((double)sum);
}

enum mh_error_code
mh_lod_select(const struct mh_lod_input *input, struct mh_lod_selection *output,
	struct mh_error *error_out)
{
	struct mh_lod_selection result;
	float delta[3], distance, scale, threshold;
	unsigned int axis, level;

	if (input == NULL || output == NULL || input->levels < 1 ||
		input->levels > MAX_LEVELS ||
		input->renderer > MH_LOD_HARDWARE ||
		(input->ignition != 0 && input->ignition != 1) ||
		(input->supercars != 0 && input->supercars != 1) ||
		(input->unavailable != 0 && input->unavailable != 1) ||
		!(input->focal >= FOCAL_MIN && input->focal <= FOCAL_MAX) ||
		!(input->width >= 1 && input->width <= VIEWPORT_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car detail input");

	for (axis = 0; axis < 3; axis++) {
		if (!(input->eye[axis] >= -POSITION_LIMIT &&
			input->eye[axis] <= POSITION_LIMIT) ||
			!(input->position[axis] >= -POSITION_LIMIT &&
				input->position[axis] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car detail position");

		delta[axis] = (float)((long double)input->eye[axis] -
			input->position[axis]);
	}

	result.level = 0;
	result.coarse = 0;

	if (!input->unavailable) {
		if (input->ignition || input->supercars) {
			result.level = input->supercars ? SUPERCARS_LEVEL
							: IGNITION_LEVEL;

			if (result.level >= input->levels)
				result.level = input->levels - 1;
		} else {
			if (input->renderer == MH_LOD_SOFTWARE) {
				distance = software_distance(delta);
				scale = (float)((double)input->focal *
					input->width);
				scale = (float)((double)scale /
					REFERENCE_WIDTH);
			} else {
				distance = (float)refined_sqrt(
					((long double)delta[1] * delta[1] +
						(long double)delta[0] *
							delta[0]) +
					(long double)delta[2] * delta[2]);
				scale = (float)((long double)input->focal *
					input->width / REFERENCE_WIDTH);
			}

			for (level = 0; level < 3; level++) {
				if (input->renderer == MH_LOD_SOFTWARE) {
					threshold =
						(float)((double)(DETAIL_DISTANCE *
								(level + 1)) *
							scale);

					if (threshold >= distance)
						break;
				} else if ((long double)(DETAIL_DISTANCE *
						(level + 1)) *
						scale >=
					distance)
					break;

				/*
				 * The software far flags change even without a
				 * fourth link.
				 */
				if (level == 2)
					result.coarse = 1;

				if (level + 1 >= input->levels)
					break;

				result.level = level + 1;

				if (input->renderer != MH_LOD_SOFTWARE)
					break;
			}
		}
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

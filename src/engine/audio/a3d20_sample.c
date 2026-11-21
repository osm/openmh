#include <string.h>

#include "engine/audio/a3d20_sample.h"

#define MIN_FAR_DISTANCE 1.1

enum mh_error_code
mh_a3d20_sample_prepare(const struct mh_audio_pcm *wave,
	const struct mh_a3d20_sample_input *input, const char *const names[],
	size_t count, struct mh_a3d20_sample_plan *output,
	struct mh_error *error_out)
{
	struct mh_spatial_buffer_input buffer;
	struct mh_a3d20_sample_plan plan;
	enum mh_error_code status;
	size_t name_index;

	if (input == NULL || input->name == NULL || output == NULL ||
		(count != 0 && names == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D 2.0 sample input");

	buffer.spatial = 1;
	buffer.software_only = 0;
	buffer.fade_in_samples = input->fade_in_samples;
	buffer.fade_out_samples = input->fade_out_samples;
	buffer.near_distance = input->near_distance;
	buffer.far_distance = input->far_distance;
	status = mh_spatial_buffer_prepare(
		wave, &buffer, &plan.buffer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* A3D 2.0 applies independent near/far distance minimums. */
	if (plan.buffer.far_distance < MIN_FAR_DISTANCE)
		plan.buffer.far_distance = (float)MIN_FAR_DISTANCE;

	/* Filename alone selects the first existing source. */
	plan.duplicate_index = count;

	for (name_index = 0; name_index < count; name_index++) {
		if (names[name_index] == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid A3D 2.0 sample name");

		if (strcmp(names[name_index], input->name) == 0) {
			plan.duplicate_index = name_index;
			break;
		}
	}

	*output = plan;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

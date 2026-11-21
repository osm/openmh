#include <string.h>

#include "engine/render/scene_finish.h"

/* Stop just before repeat would wrap the motion. */
#define FINISH_END_MARGIN 0.01
#define FINISH_TIME_LIMIT 1e9f

static void
camera_pose(const struct mh_scene_object *pose, const float camera[16],
	float model[16])
{
	float local[12];
	long double value;
	unsigned int row, column;

	for (row = 0; row < 9; row++)
		local[row] = (float)pose->rotation[row];

	for (row = 0; row < 3; row++)
		local[9 + row] = (float)pose->position[row];

	memset(model, 0, 16 * sizeof(*model));
	model[15] = 1;

	/* Camera * local, with one binary32 store per component. */
	for (column = 0; column < 4; column++)
		for (row = 0; row < 3; row++) {
			value = (long double)local[column * 3 + 1] *
				camera[4 + row];
			value += (long double)local[column * 3] * camera[row];
			value += (long double)local[column * 3 + 2] *
				camera[8 + row];

			if (column == 3)
				value += camera[12 + row];

			model[column * 4 + row] = (float)value;
		}
}

enum mh_error_code
mh_finish_frame(const struct mh_finish_assets *assets, unsigned int rank,
	float elapsed, const float camera[16], float *seconds,
	struct mh_finish_frame *output, struct mh_error *error_out)
{
	struct mh_finish_frame frame;
	struct mh_scene_object pose;
	long double next;
	float end;
	unsigned int channel;
	enum mh_error_code status;

	if (assets == NULL || seconds == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing finish sign state");

	memset(&frame, 0, sizeof(frame));

	if (rank == 0 || rank > MH_FINISH_SIGNS) {
		*output = frame;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (camera == NULL || assets->motion.key_count == 0 ||
		assets->motion.keys == NULL ||
		!(elapsed >= 0 && elapsed <= FINISH_TIME_LIMIT) ||
		!(*seconds >= 0 && *seconds <= FINISH_TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish sign motion");

	status = mh_scene_sample(&assets->motion, *seconds, &pose, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	frame.rank = rank;
	camera_pose(&pose, camera, frame.model);

	for (channel = 0; channel < 3; channel++)
		frame.color[channel] = (float)pose.color[channel];

	end = (float)assets->motion.keys[assets->motion.key_count - 1].time;
	next = (long double)*seconds + elapsed;

	/* The original compares the unrounded sum after storing binary32. */
	*seconds = next > end ? (float)((long double)end - FINISH_END_MARGIN)
			: (float)next;

	*output = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

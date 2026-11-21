#ifndef MH_TRACK_CAMERA_H
#define MH_TRACK_CAMERA_H

#include "engine/core/error.h"
#include "engine/navigation/camera_motion.h"
#include "engine/simulation/camera.h"

/*
 * F3 selects the first nearest anchor and decodes the original unsigned
 * selector remainder. Bounds errors leave both outputs untouched.
 */
enum mh_error_code	mh_track_camera_select(
	const struct mh_camera_motion *motion, const float position[3],
	size_t *index, unsigned int *mode, struct mh_error *error_out);

/*
 * Original F3 camera branches and dynamic focal/far settings. Prior basis
 * matters when a look-at target coincides with its eye. Output is atomic;
 * interpolated modes reject coincident anchors (original produces NaNs).
 */
enum mh_error_code	mh_track_camera_view(
	const struct mh_camera_pose *previous, const struct mh_camera_pose *car,
	const struct mh_camera_motion *motion, float default_far, float aspect,
	struct mh_camera_pose *output, struct mh_camera_lens *lens,
	struct mh_error *error_out);

#endif

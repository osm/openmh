#ifndef MH_CAMERA_H
#define MH_CAMERA_H

#include "engine/core/error.h"

/*
 * Column-major right/up/forward basis followed by world position, matching
 * the original mathematical layout without aliasing disk/runtime structs.
 */
struct mh_camera_pose {
	float	rotation[9];
	float	position[3];
};

#define MH_CAMERA_RACE_NEAR .5f
#define MH_CAMERA_RACE_FOCAL 1.0f

enum mh_camera_mode {
	MH_CAMERA_CHASE,
	MH_CAMERA_IN_CAR,
	MH_CAMERA_TRACK
};

struct mh_camera_roll {
	/* Temporarily leave requested in-car view. */
	int	external;

	/* Cumulative three-wheel support. */
	float	supported_seconds;
};

struct mh_camera_lens {
	float	focal, near_depth, far_depth, pixel_aspect;
};

/*
 * Original F4 membership cycle. Unavailable racers remain eligible; only
 * absent entries are skipped. Invalid/empty membership leaves output alone.
 */
enum mh_error_code	mh_camera_cycle(unsigned int current,
	unsigned int count, unsigned int active_mask, unsigned int *selected,
	struct mh_error *error_out);

/* Inversion latch and strict >.5 supported-time release. */
enum mh_error_code	mh_camera_roll_step(const struct mh_camera_roll *state,
	float up_y, const int supported[4], float elapsed_seconds,
	struct mh_camera_roll *output, struct mh_error *error_out);

/*
 * GoalCamera world eye/target through the shared look-at helper. Prior right/up
 * survive a coincident target; output may alias previous.
 */
enum mh_error_code	mh_camera_goal_view(
	const struct mh_camera_pose *previous, const float eye[3],
	const float target[3], struct mh_camera_pose *output,
	struct mh_error *error_out);

/*
 * Opening camera: unlike GoalCamera, a vertical target also preserves the prior
 * up column. Eye comes from a start motion, target from the local player's
 * displayed body origin. Lens/listener state is separate.
 */
enum mh_error_code	mh_camera_intro_view(
	const struct mh_camera_pose *previous, const float eye[3],
	const float target[3], struct mh_camera_pose *output,
	struct mh_error *error_out);

/*
 * Supercars override: fixed downward basis, eye 25 above the body and 12 along
 * its horizontal forward components. No smoothing or look-back rotation. Output
 * may alias car; error is atomic.
 */
enum mh_error_code	mh_camera_supercars_view(
	const struct mh_camera_pose *car, struct mh_camera_pose *output,
	struct mh_error *error_out);

/*
 * Original lens clamps and viewport correction. aspect is the float width /
 * height supplied by the original backend. Invalid inputs leave output alone.
 */
enum mh_error_code	mh_camera_lens_setup(float focal, float near_depth,
	float far_depth, float aspect, struct mh_camera_lens *output,
	struct mh_error *error_out);

/*
 * Follow camera, with an optional overhead view. Eye offset is car-local;
 * target offset is world-relative in ordinary mode. Overhead uses fixed offsets
 * and ignores both offset inputs. elapsed_seconds is frame time, not
 * physics-slice time. May alias previous. Degenerate bases are preserved;
 * invalid bounds leave output alone.
 */
enum mh_error_code	mh_camera_follow(const struct mh_camera_pose *previous,
	const struct mh_camera_pose *car, const float eye_offset[3],
	const float target_offset[3], float elapsed_seconds, int overhead,
	struct mh_camera_pose *output, struct mh_error *error_out);

/*
 * Native presentation correction: carry the prior chase eye with the car's
 * translation before applying the follower. Eliminates translation- induced
 * pitch oscillation when display and physics rates differ.
 */
enum mh_error_code	mh_camera_follow_motion(
	const struct mh_camera_pose *previous, const float previous_car[3],
	const struct mh_camera_pose *car, float elapsed_seconds,
	struct mh_camera_pose *output, struct mh_error *error_out);

/*
 * Normal race branches: chase, car-attached in-car, and their look-back poses.
 * mode is the effective view after the inversion latch. Output may alias an
 * input; failure is atomic. Hidden/intro/track camera selection is separate.
 */
enum mh_error_code	mh_camera_view(const struct mh_camera_pose *previous,
	const struct mh_camera_pose *car, float elapsed_seconds,
	enum mh_camera_mode mode, int look_back, struct mh_camera_pose *output,
	struct mh_error *error_out);

#endif

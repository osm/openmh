#ifndef MH_AUDIO_SOUND_SPATIAL_H
#define MH_AUDIO_SOUND_SPATIAL_H

#include "engine/core/error.h"
/* Native validation bounds for world positions and attenuation ranges. */
#define MH_SOUND_POSITION_LIMIT 1000000
#define MH_SOUND_DISTANCE_MAX 1000000

struct mh_sound_listener {
	double	position[3], right[3], forward[3];
};
struct mh_motion_state;
struct mh_camera_pose;

struct mh_sound_spatial_result {
	double	gains[2];

	/* Positive distance attenuation, independently of gain. */
	int	eligible;
};

enum mh_error_code	mh_sound_spatial_prepare(
	const struct mh_sound_listener *listener, const double source[3],
	double volume, double near_distance, double far_distance,
	struct mh_sound_spatial_result *output, struct mh_error *error_out);

/*
 * Car records bind the display origin, not the internal physics origin.
 * Uses the same local-Y correction as rendering, including remote cars.
 */
enum mh_error_code	mh_sound_vehicle_spatial_prepare(
	const struct mh_sound_listener *listener,
	const struct mh_motion_state *motion, double volume,
	double near_distance, double far_distance,
	struct mh_sound_spatial_result *output, struct mh_error *error_out);

/*
 * Directional gains; axes supplied in world coordinates. No Doppler. Output
 * unchanged on invalid inputs.
 */
enum mh_error_code	mh_sound_spatial(
	const struct mh_sound_listener *listener, const double source[3],
	double volume, double near_distance, double far_distance,
	double gains[2], struct mh_error *error_out);

/*
 * Bind the evaluated camera transform. Preserve its right/forward columns,
 * including roll and degenerate bases. Caller supplies a valid pose; positional
 * preparation validates the copied values.
 */
void	mh_sound_listener_pose(const struct mh_camera_pose *pose,
	struct mh_sound_listener *listener);

/*
 * Diagnostic upright camera listener; direction is normalized with a safe
 * identity fallback for a degenerate view. Inputs from validated simulation.
 */
void	mh_sound_listener_camera(const double eye[3], const double target[3],
	struct mh_sound_listener *listener);

#endif

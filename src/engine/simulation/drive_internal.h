#ifndef MH_DRIVE_INTERNAL_H
#define MH_DRIVE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/simulation/drive.h"

#define MH_DRIVE_TIME_LIMIT 1e9

/* Seed the camera only for a fresh native driving state. */
enum mh_error_code	mh_drive_camera_start(struct mh_drive *drive,
	struct mh_error *error_out);

/*
 * Publish accumulated body effects after each local slice. Missing spark
 * records retain prior gains/corners. Failure is atomic.
 */
enum mh_error_code	mh_drive_body_publish(struct mh_drive *drive,
	double entry_up_y, struct mh_error *error_out);

/* Role-1 cars retain body publications; all cars clear pair maxima. */
void	mh_drive_effects_begin_frame(struct mh_drive *drive);

/*
 * Once after all frame physics and wheel presentation, before camera update.
 * Uses final support/contact/AI feedback and unhalved frame time. Fields use
 * the shared camera retained from the preceding presentation.
 * No clock advance; failure preserves the whole drive.
 */
enum mh_error_code	mh_drive_recovery_frame(struct mh_drive *drive,
	const float camera[3], float elapsed, struct mh_error *error_out);

/*
 * Every active car, including remote/unviewed cars, after recovery. Applying
 * the selected camera later may clear only that car's latch.
 */
enum mh_error_code	mh_drive_camera_roll_frame(struct mh_drive *drive,
	float elapsed, struct mh_error *error_out);

/* Frame clock before physics, race progress after recovery/camera. */
enum mh_error_code	mh_drive_frame_begin(struct mh_drive *drive,
	long double elapsed, struct mh_error *error_out);

/*
 * Restart samples the lifetime counter again but retains this frame's
 * interval/previous timestamp. Reset only the intro/GO/race clock products.
 */
enum mh_error_code	mh_drive_clock_restart(struct mh_drive *drive,
	long double elapsed, struct mh_error *error_out);
enum mh_error_code	mh_drive_frame_finish(struct mh_drive *drive,
	struct mh_error *error_out);

/*
 * Current selected physical slice, or the fixed diagnostic interval. Slow
 * motion halves the whole frame input before subdivision, including AI. Race
 * clocks, recovery and presentation retain unhalved time.
 */
double			mh_drive_physics_dt(const struct mh_drive *drive);
enum mh_error_code	mh_drive_integrate_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out);

/*
 * Field alternative: road integration for all cars, shared collision passes,
 * then finish slice accounting for all cars. Wheels and camera advance once at
 * the enclosing presentation boundary. Field owns the rollback.
 */
enum mh_error_code	mh_drive_road_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out);
enum mh_error_code	mh_drive_finish_tick(struct mh_drive *drive,
	struct mh_error *error_out);

/* Unavailable local car: engine/steering only, followed by finish_tick. */
enum mh_error_code	mh_drive_engine_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out);

#endif

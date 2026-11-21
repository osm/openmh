#include <string.h>

#include "engine/core/error.h"
#include "engine/render/scene.h"
#include "engine/simulation/drive_particles.h"

#define SCENE_SECONDS_LIMIT 1e9
#define FRAME_INTERVAL_LIMIT 10
#define EMISSION_INTERVAL .05

void
mh_race_scene_reset_particles(struct mh_race_scene *scene)
{
	memset(scene->visuals.particles, 0, sizeof(scene->visuals.particles));
	mh_particle_system_init(&scene->visuals.particle_system,
		scene->assets.profile != MH_RENDER_SOFTWARE, 1, NULL);
	scene->visuals.particle_timer = 0;
	scene->visuals.particle_seconds = 0;
	scene->visuals.particle_mask = 0;
}

enum mh_error_code
mh_race_scene_update_particles(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_error *error_out)
{
	struct mh_particles pools[MH_RACE_MAX_CARS];
	struct mh_particle_system system;
	unsigned int entry, listed_mask, emitting_mask;
	float timer, elapsed;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		!(field->scene_seconds >= 0 &&
			field->scene_seconds <= SCENE_SECONDS_LIMIT) ||
		!(scene->visuals.particle_seconds >= 0 &&
			scene->visuals.particle_seconds <=
				SCENE_SECONDS_LIMIT) ||
		!(scene->visuals.particle_timer >= 0 &&
			scene->visuals.particle_timer <=
				FRAME_INTERVAL_LIMIT) ||
		field->active_mask >= (1U << field->count) ||
		(field->unavailable_mask & ~field->active_mask))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid particle field");

	memcpy(pools, scene->visuals.particles, sizeof(pools));
	system = scene->visuals.particle_system;
	timer = scene->visuals.particle_timer;
	listed_mask = scene->visuals.particle_mask;

	/*
	 * Retain pools, animation cursors, frame timing and the shared RNG.
	 * Restart rebases scene animation, not the particle interval. Without a
	 * sampled frame, a backward diagnostic clock supplies zero.
	 */
	elapsed = field->scene_seconds < scene->visuals.particle_seconds
		? 0
		: (float)(field->scene_seconds -
			scene->visuals.particle_seconds);

	/*
	 * Add the unhalved, capped frame interval, not the uncapped scenery
	 * clock delta. Diagnostic callers retain their clock.
	 */
	if (field->cars[0].frame.active)
		elapsed = field->cars[0].frame.time.elapsed;

	if (!(elapsed >= 0 && elapsed <= FRAME_INTERVAL_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"particle frame interval exceeds bounds");

	emitting_mask = field->active_mask & ~field->unavailable_mask;
	listed_mask |= emitting_mask;

	if (mh_drive_camera_in_car(&field->cars[mh_field_viewed(field)]) ||
		mh_drive_camera_look_back(&field->cars[mh_field_viewed(field)]))
		emitting_mask &= ~(1U << mh_field_viewed(field));

	if ((double)timer > EMISSION_INTERVAL) {
		for (entry = 0; entry < field->count; entry++)
			if (emitting_mask & (1U << entry)) {
				status = mh_drive_particles_emit(
					&field->cars[entry], &system,
					&pools[entry], error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

		for (entry = 0; entry < MH_RACE_MAX_CARS; entry++)
			if (listed_mask & (1U << entry)) {
				status = mh_particles_advance(&system, timer,
					&pools[entry], error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

		timer = 0;
	} else
		timer = (float)((long double)timer + elapsed);

	memcpy(scene->visuals.particles, pools, sizeof(pools));
	scene->visuals.particle_system = system;
	scene->visuals.particle_timer = timer;
	scene->visuals.particle_mask = listed_mask;
	scene->visuals.particle_seconds = field->scene_seconds;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#include <stddef.h>

#include "engine/render/scene.h"

void
mh_race_scene_bind_cameras(
	const struct mh_race_scene *scene, struct mh_field *field)
{
	struct mh_drive *drive;
	unsigned int entry;

	for (entry = 0; entry < field->count; entry++) {
		drive = &field->cars[entry];

		/*
		 * Binding is repeated on entry to the renderer. Only a fresh
		 * session receives the original initial in-car preference and
		 * intro latch.
		 */
		if (drive->start_camera[0] == NULL &&
			drive->start_camera[1] == NULL && drive->ticks == 0 &&
			drive->start_ticks == 0 && drive->countdown &&
			(scene->assets.start_camera[0].key_count ||
				scene->assets.start_camera[1].key_count)) {
			drive->camera_intro = drive->camera_saved_in_car = 1;
			drive->camera_mode = MH_CAMERA_IN_CAR;
		}

		drive->camera_motion = scene->assets.camera_motion.count != 0
			? &scene->assets.camera_motion
			: NULL;
		drive->start_camera[0] = scene->assets.start_camera[0].key_count
			? &scene->assets.start_camera[0]
			: NULL;
		drive->start_camera[1] = scene->assets.start_camera[1].key_count
			? &scene->assets.start_camera[1]
			: NULL;
	}
}

#include "engine/audio/spatial_scene.h"

void
mh_spatial_scene_start(struct mh_spatial_scene *scene, int sounds_ready,
	int device_selected, const struct mh_camera_pose *camera)
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		scene->stationary[axis] = 0;

	mh_spatial_scene_bind(scene, sounds_ready, device_selected, camera,
		scene->stationary, 1);
}

void
mh_spatial_scene_bind(struct mh_spatial_scene *scene, int sounds_ready,
	int device_selected, const struct mh_camera_pose *camera,
	const float secondary[3], int in_car)
{
	if (!sounds_ready)
		return;

	scene->in_car = in_car != 0;

	if (!device_selected)
		return;

	scene->camera = camera;
	scene->secondary = secondary;
}

int
mh_spatial_scene_frame(int device_selected, const struct mh_camera_pose *camera,
	float elapsed, const struct mh_world *world, float reverb,
	struct mh_spatial_frame *output)
{
	if (!device_selected)
		return 0;

	output->camera = camera;
	output->elapsed = elapsed;
	output->world = world;
	output->reverb = reverb;

	return 1;
}

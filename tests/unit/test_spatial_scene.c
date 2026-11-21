#include <stdio.h>
#include <string.h>

#include "engine/audio/spatial_scene.h"
#include "engine/core/binary.h"
#include "engine/geometry/world.h"
#include "engine/simulation/camera.h"

enum {
	PLAYER_COUNT = 8
};

struct fixture {
	struct mh_world		world;
	unsigned long		world_word;
	struct mh_spatial_scene	scene;
	struct mh_camera_pose	camera;
	float			secondary[PLAYER_COUNT][3];
};

static int
sequence(void)
{
	struct fixture fixture;
	struct mh_spatial_frame frame;
	struct mh_spatial_scene *scene;

	memset(&fixture, 0, sizeof(fixture));
	scene = &fixture.scene;
	mh_spatial_scene_start(scene, 1, 1, &fixture.camera);

	if (!scene->in_car || scene->secondary != scene->stationary)
		return 1;

	mh_spatial_scene_bind(
		scene, 1, 1, &fixture.camera, fixture.secondary[3], -2);
	fixture.camera.position[0] = 42;
	fixture.secondary[3][0] = 17;

	if (!scene->in_car || scene->camera->position[0] != 42 ||
		scene->secondary[0] != 17)
		return 1;

	mh_spatial_scene_bind(scene, 1, 0, NULL, NULL, 0);

	if (scene->in_car || scene->camera != &fixture.camera)
		return 1;

	mh_spatial_scene_bind(scene, 0, 1, NULL, NULL, 1);

	if (scene->in_car || scene->secondary != fixture.secondary[3])
		return 1;

	frame.camera = &fixture.camera;
	frame.reverb = -3;

	if (mh_spatial_scene_frame(0, NULL, 0, 0, 0, &frame) ||
		frame.camera != &fixture.camera || frame.reverb != -3)
		return 1;

	if (!mh_spatial_scene_frame(1, NULL, 0, 0, 0, &frame) ||
		frame.camera != NULL || frame.world != NULL ||
		frame.elapsed != 0 || scene->secondary != fixture.secondary[3])
		return 1;

	scene->stationary[0] = 12;
	mh_spatial_scene_start(scene, 0, 0, &fixture.camera);

	if (scene->stationary[0] != 0 || scene->in_car ||
		scene->secondary != fixture.secondary[3])
		return 1;

	puts("Spatial scene bindings, borrowed motion, gates and menu update passed");

	return 0;
}

int
main(void)
{
	return sequence();
}

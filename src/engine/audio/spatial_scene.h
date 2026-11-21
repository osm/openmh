#ifndef MH_SPATIAL_SCENE_H
#define MH_SPATIAL_SCENE_H

struct mh_camera_pose;
struct mh_world;

/*
 * Shared game-side bindings, not driver listener history. Camera and secondary
 * are borrowed until replaced. Keep this owner at a stable address: startup
 * binds secondary to its stationary vector. Initialize once to zero. The normal
 * race anchor is camera->position, NOT the viewed car's position. Secondary
 * follows the viewed car's runtime vector; its interpretation by each driver
 * remains separate from this binding policy.
 */
struct mh_spatial_scene {
	const struct mh_camera_pose	*camera;
	const float			*secondary;
	float				stationary[3];
	int				in_car;
};

struct mh_spatial_frame {
	const struct mh_camera_pose	*camera;
	const struct mh_world		*world;
	float				elapsed, reverb;
};

/* Zero the startup vector even when binding is gated off. */
void	mh_spatial_scene_start(struct mh_spatial_scene *scene, int sounds_ready,
	int device_selected, const struct mh_camera_pose *camera);

/*
 * The race caller normalizes any nonzero effective in-car flag. Without loaded
 * sounds nothing changes; without a device only in_car changes. No voice stop
 * or history reset.
 */
void	mh_spatial_scene_bind(struct mh_spatial_scene *scene, int sounds_ready,
	int device_selected, const struct mh_camera_pose *camera,
	const float secondary[3], int in_car);

/*
 * Return whether to dispatch an update; leave output untouched without a
 * selected device. Borrow camera for that update. Race inputs are the current
 * camera, elapsed seconds, the borrowed decoded world and the viewed car's
 * published raw route sound_blend. Menus pass NULL camera and three zeros; they
 * do not clear scene bindings. Queue processing still runs at zero elapsed;
 * listener policy gates motion.
 */
int	mh_spatial_scene_frame(int device_selected,
	const struct mh_camera_pose *camera, float elapsed,
	const struct mh_world *world, float reverb,
	struct mh_spatial_frame *output);

#endif

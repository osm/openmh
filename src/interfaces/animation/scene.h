#ifndef MH_INTERFACES_ANIMATION_SCENE_H
#define MH_INTERFACES_ANIMATION_SCENE_H

#include <stddef.h>

#define MH_SCENE_MODEL_NAME_SIZE 256

enum mh_scene_end_behavior {
	MH_SCENE_HOLD = 1,
	MH_SCENE_REPEAT = 2
};

struct mh_scene_key {
	/* Scene seconds (route: raw frames); increasing from zero. */
	double	time;
	double	value[9];
	double	tension;
	double	continuity;
	double	bias;

	/* Selects interpolation of the segment arriving here. */
	int	linear;
};

/*
 * Pose and owned motion in game coordinates. The last three motion
 * channels drive RGB in Motorhead; they do not scale the geometry.
 */
struct mh_scene_object {
	/* Logical model basename, resolved under the track object directory. */
	char			model[MH_SCENE_MODEL_NAME_SIZE];
	double			position[3];

	/* Column-major, maps local vectors into the world. */
	double			rotation[9];
	double			color[3];
	struct mh_scene_key	*keys;
	size_t			key_count;
	int			end_behavior;
	int			has_edge_color;

	/* ObjEdgeColor first/second values. */
	unsigned int		sound_class, collision_sound_class;
	int	unseen_by_rays, has_shadow_options, has_polygon_size;

	/* Original mass input, multiplied by 100, not geometry scale. */
	double			polygon_size;
};

struct mh_scene {
	struct mh_scene_object	*objects;
	size_t			object_count;
	size_t			key_count;
	double			frames_per_second;
};

#endif

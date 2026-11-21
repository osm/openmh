#ifndef MH_SCENE_EFFECT_H
#define MH_SCENE_EFFECT_H

/* Scene mesh policy, independent of the graphics API used to display it. */
enum mh_scene_effect {
	MH_SCENE_EFFECT_SKY = -1,
	MH_SCENE_EFFECT_OPAQUE = 0,
	MH_SCENE_EFFECT_SKIDS = 1,
	MH_SCENE_EFFECT_PARTICLES = 2,
	MH_SCENE_EFFECT_UNAVAILABLE = 3,
	MH_SCENE_EFFECT_HALOS = 4,
	MH_SCENE_EFFECT_SHADOWS = 5,
	MH_SCENE_EFFECT_PASSES = 7
};

#endif

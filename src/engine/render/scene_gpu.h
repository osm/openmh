#ifndef MH_SCENE_GPU_H
#define MH_SCENE_GPU_H

#include "engine/render/scene.h"
#include "interfaces/render/renderer.h"

/* Effect mesh offsets after world and cars in the hardware scene. */
enum {
	MH_SCENE_GPU_SKID_MESH,
	MH_SCENE_GPU_PARTICLE_MESH,
	MH_SCENE_GPU_SKY_MESH,
	MH_SCENE_GPU_LAMP_MESH,
	MH_SCENE_GPU_SHADOW_MESH,
	MH_SCENE_GPU_NAME_MESH,
	MH_SCENE_GPU_EFFECT_MESHES
};

enum mh_error_code	mh_race_scene_gpu_order(struct mh_renderer *renderer,
	const struct mh_race_scene *scene, unsigned int cars,
	unsigned int finish_count, struct mh_error *error_out);

#endif

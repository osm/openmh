#ifndef MH_SOFTWARE_SCENE_ASSETS_H
#define MH_SOFTWARE_SCENE_ASSETS_H

#include "engine/assets/name_images.h"
#include "engine/render/software_scene_halo_assets.h"
#include "engine/render/software_scene_particle_assets.h"
#include "engine/simulation/ai_traffic.h"

/* Owned by the race scene; no hardware atlas or prepared backend pixels. */
struct mh_software_scene_assets {
	struct mh_name_images			names;
	struct mh_image				sky;
	struct mh_indexed_image			environment[3], lens;
	struct mh_software_particle_assets	particles;
	struct mh_software_halo_assets		track_halos;
	struct mh_software_halo_assets		car_halos[MH_RACE_MAX_CARS];
};

void	mh_software_scene_assets_free(struct mh_software_scene_assets *assets);

#endif

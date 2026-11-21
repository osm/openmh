#ifndef MH_RACE_SCENE_ASSETS_H
#define MH_RACE_SCENE_ASSETS_H

#include "engine/assets/object_assets.h"
#include "engine/navigation/camera_motion.h"
#include "engine/render/mesh.h"
#include "engine/render/profile.h"
#include "engine/render/scene_finish.h"
#include "engine/render/scene_matrices.h"
#include "engine/render/software_scene_assets.h"
#include "engine/simulation/field.h"
#include "interfaces/image/pixels.h"
#include "interfaces/retro/data.h"

/*
 * Resources owned by mh_race_scene and released by mh_race_scene_free.
 * Simulation borrows decoded collision data; lighting borrows decoded world
 * data. Runtime visual state is kept separately in mh_race_visuals.
 */
struct mh_race_scene_assets {
	enum mh_render_profile		profile;

	/* Fixed material/map sampling precision for this asset lifetime. */
	enum mh_texture_format		opaque_format, alpha_format;
	struct mh_software_scene_assets	software;
	struct mh_car			car;
	struct mh_track			track;

	/* One-based installed track catalog number. */
	unsigned int			track_number;
	struct mh_collision		world_collision;
	struct mh_collision		car_hulls[MH_RACE_MAX_CARS];
	struct mh_materials		materials;
	struct mh_route			route;
	struct mh_camera_motion		camera_motion;

	/* Owned standalone LWMO keys. */
	struct mh_scene_object		start_camera[2];
	struct mh_ai_path		ai_path;
	struct mh_mesh			meshes[MH_FIELD_MESHES];
	struct mh_finish_assets		finish;

	/* Selected atlas page order. */
	struct mh_texture_groups	texture_groups;

	/* Owned shared original pages. */
	struct mh_atlas			texture_atlas;

	/*
	 * PowerVR halo/flare/shadow/name page; effect meshes borrow its pixels.
	 */
	struct mh_atlas			lamp_atlas;

	/* Original palette for later roster preparation in the shared page. */
	unsigned char			lamp_palette[MH_IMAGE_PALETTE_BYTES];

	/* Original-format bytes, not GPU allocation. */
	unsigned long			texture_budget;

	/* remap0 or remap1; PowerVR always selects remap1. */
	unsigned int			texture_quality;
	struct mh_car			opponents[MH_RACE_MAX_CARS - 1];
	struct mh_object_assets		objects;
	struct mh_world			lighting_world;
	struct mh_retro_assets		*retro;

	/* Fixed for this asset lifetime. */
	unsigned int			view_width, view_height;

	/* High=0, Medium=1, Low=2. */
	unsigned int			body_start_detail;

	/* Also gates object-bound ambient emitters. */
	int				scenery;
};

#endif

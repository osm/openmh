#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_SCENE_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_SCENE_H

#include "engine/render/scene.h"
#include "engine/render/software_scene_halo_assets.h"
#include "engine/render/software_scene_particle_assets.h"

/*
 * Native scene loaders; select the filesystem resource source.
 * Asset ownership, profile and later-load rules follow the corresponding
 * portable _from operations.
 */
enum mh_error_code	mh_race_scene_load(const char *root, unsigned int car,
	unsigned int track, int scenery, struct mh_race_scene *scene,
	struct mh_error *error_out);

enum mh_error_code	mh_race_scene_load_settings(const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, struct mh_race_scene *scene,
	struct mh_error *error_out);

enum mh_error_code	mh_race_scene_names_prepare(const char *root,
	struct mh_race_scene *scene, const struct mh_field *field,
	const struct mh_profile roster[MH_NAME_LABELS_MAX], unsigned int count,
	struct mh_error *error_out);

enum mh_error_code	mh_race_scene_load_ai(const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);

enum mh_error_code	mh_race_scene_add_opponents(const char *root,
	unsigned int car, unsigned int track, unsigned int count,
	struct mh_race_scene *scene, struct mh_error *error_out);

#endif

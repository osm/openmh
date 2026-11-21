#ifndef MH_SOFTWARE_SCENE_HALOS_H
#define MH_SOFTWARE_SCENE_HALOS_H

#include "interfaces/geometry/primitive.h"
#include "interfaces/render/halos.h"

struct mh_race_scene;
struct mh_field;
struct mh_world_view;
struct mh_av_settings;

enum mh_software_halo_kind {
	MH_SOFTWARE_HALO_WORLD,
	MH_SOFTWARE_HALO_CAR,
	MH_SOFTWARE_HALO_SCENERY
};

/* Reusable source storage; images and car transforms remain borrowed. */
struct mh_software_scene_halos {
	struct mh_renderer_halo	*sources;
	size_t			count, capacity;
};

/* Original camera-independent caller style, before image/pose binding. */
enum mh_error_code	mh_software_halo_style(enum mh_software_halo_kind kind,
	const struct mh_geometry_halo *source, int braking,
	struct mh_renderer_halo *output, struct mh_error *error_out);

/*
 * Reuse sufficient capacity. Successful growth replaces storage and clears
 * count.
 */
enum mh_error_code	mh_race_scene_prepare_software_halos(
	struct mh_race_scene *scene, unsigned int cars,
	struct mh_error *error_out);

/*
 * Rebuild in world/car/scenery order. Failed updates may leave a partial list;
 * successful sources borrow images and model matrices through rendering.
 */
enum mh_error_code	mh_race_scene_update_software_halos(
	struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, const struct mh_world_view *view,
	const struct mh_av_settings *settings, struct mh_error *error_out);

#endif

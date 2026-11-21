#ifndef MH_SOFTWARE_SCENE_H
#define MH_SOFTWARE_SCENE_H

#include "engine/render/scene.h"
#include "interfaces/render/renderer.h"

/* Mesh layout: world, car meshes, one shadow mesh, then finish signs. */
#define MH_SCENE_SOFTWARE_EFFECT_MESHES 1

/*
 * Borrowed frame inputs; scene owns effect history, caller owns UI and
 * matrices.
 */
struct mh_scene_software_frame {
	const struct mh_av_settings	*settings;
	unsigned int			width, height;
	const float			*models, *view;
	const struct mh_ui_list		*ui;
	unsigned int	visible_roots, shading, perspective, names_mode;
};

/* Scene parts interleave shadows and cars; name labels attach to body parts. */
size_t	mh_race_scene_software_car_part(unsigned int car);

/* Span zero is the world; subsequent spans follow all car groups. */
size_t			mh_race_scene_software_world_part(unsigned int cars,
	size_t span);
enum mh_error_code	mh_race_scene_software_order(
	struct mh_renderer *renderer, const struct mh_mesh *world,
	unsigned int cars, unsigned int finish_count,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_software_setup(
	const struct mh_race_scene *scene, struct mh_renderer *renderer,
	unsigned int cars, unsigned int finish_count,
	const struct mh_av_settings *settings, struct mh_error *error_out);

/* Rendering failure does not commit skid visibility history. */
enum mh_error_code	mh_race_scene_software_frame(
	struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_renderer *renderer,
	const struct mh_scene_software_frame *frame,
	struct mh_error *error_out);

#endif

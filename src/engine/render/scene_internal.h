#ifndef MH_RACE_SCENE_INTERNAL_H
#define MH_RACE_SCENE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/render/scene.h"
#include "interfaces/geometry/decoder.h"

struct mh_resource_source;

enum mh_error_code	mh_race_scene_open_opaque_textures(
	const struct mh_resource_source *resources, const char *root,
	const struct mh_race_scene *scene, struct mh_image_collection **output,
	struct mh_error *error_out);

enum mh_error_code	mh_race_scene_load_lamps_from(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_lamp_atlas(
	struct mh_race_scene *scene, const struct mh_file *glow,
	const struct mh_file *flare, struct mh_error *error_out);

enum mh_error_code	mh_race_scene_brightness(struct mh_race_scene *scene,
	const struct mh_weather_style *style, int dimmed,
	const struct mh_av_settings *settings, struct mh_error *error_out);

/*
 * Private loading stages. Files returned by read_asset/read_table belong to
 * the caller. Scene allocations survive stage errors and are released by
 * mh_race_scene_free(); opponent loading retains the partial scene on error.
 */
enum mh_error_code	mh_race_scene_load_car_part(
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *stem, int body, unsigned int minimum_detail,
	const struct mh_image_collection *primary,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_car(
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, unsigned int entry, struct mh_race_scene *scene,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_track_config(
	const struct mh_resource_source *resources, const char *root,
	unsigned int track, int *scenery, struct mh_race_scene *scene,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_track_data(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_world(
	const struct mh_resource_source *resources, const char *root,
	int scenery, const struct mh_image_collection *primary,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_software(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_effects(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_texture_budget(
	enum mh_render_profile profile, const unsigned long *override,
	unsigned long *budget, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_register_textures(
	const struct mh_resource_source *resources, const char *root,
	const struct mh_image_collection *cutout,
	const struct mh_image_collection *opaque, struct mh_race_scene *scene,
	unsigned int mesh_count, struct mh_error *error_out);

#endif

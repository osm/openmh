#ifndef MH_RACE_SCENE_H
#define MH_RACE_SCENE_H

#include "engine/assets/name_mesh.h"
#include "engine/core/error.h"
#include "engine/render/atmosphere_lamps.h"
#include "engine/render/brightness.h"
#include "engine/render/effect_particles.h"
#include "engine/render/effect_shadows.h"
#include "engine/render/effect_skids.h"
#include "engine/render/profile.h"
#include "engine/render/retro.h"
#include "engine/render/scene_assets.h"
#include "engine/render/scene_glenz.h"
#include "engine/render/scene_loading.h"
#include "engine/render/scene_matrices.h"
#include "engine/render/software_scene_halos.h"
#include "engine/render/viewport.h"
#include "engine/settings/av.h"
#include "interfaces/collision/source.h"
#include "interfaces/geometry/decoder.h"
#include "interfaces/image/atlas_source.h"
#include "interfaces/render/lighting.h"
#include "interfaces/render/names.h"

struct mh_horn_source;
struct mh_renderer;

struct mh_env_sound_source;
struct mh_material_source;
struct mh_catalog_source;
struct mh_navigation_source;
struct mh_retro_source;
struct mh_animation_source;
struct mh_font_source_provider;

struct mh_resource_source;
struct mh_image_decoder_provider;

struct mh_image_source_provider;

/*
 * Mutable visual state, retained across recovery and reset only as documented.
 */
struct mh_race_visuals {
	struct mh_scene_glenz		glenz;
	struct mh_prepared_light	*prepared_lights;

	/* Software far-detail shade/reflection switch for each car root. */
	int				coarse[MH_RACE_MAX_CARS];
	struct mh_vertex		*shaded_vertices[MH_FIELD_MESHES];
	struct mh_vertex		*finish_vertices[MH_FINISH_SIGNS];
	struct mh_skids			skids[MH_RACE_MAX_CARS];
	struct mh_mesh			skid_mesh;
	double				skid_seconds;
	struct mh_particle_system	particle_system;
	struct mh_particles		particles[MH_RACE_MAX_CARS];
	struct mh_mesh			particle_mesh;
	struct mh_mesh			sky_mesh;
	struct mh_lamps			lamps;
	struct mh_software_scene_halos	software_halos;
	struct mh_name_mesh		names;
	struct mh_mesh			shadow_mesh;
	double				particle_seconds;
	float				particle_timer;

	/* Previously listed pools keep aging. */
	unsigned int			particle_mask;

	const struct mh_weather_style
		*weather; /* Immutable; NULL for track defaults. */

	/* Menu factor before the renderer's weather override. */
	int				pause_dim;
	struct mh_brightness		brightness;
	int brightness_ready; /* All-zero brightness is also a valid first
				frame. */
	/* Grid recovery does not recreate the renderer. */
	float				tron_time;

	/*
	 * Grid reset retains this frame; renderer creation restores process
	 * history.
	 */
	struct mh_retro_state		retro_state;
};

struct mh_race_scene {
	/* Borrowed through scene teardown, including later car/effect loads. */
	const struct mh_image_source_provider	*image_source;
	const struct mh_model_decoder		*model_decoder;
	const struct mh_world_decoder		*world_decoder;
	const struct mh_atlas_source		*atlas_source;
	const struct mh_animation_source	*animation_source;
	const struct mh_catalog_source		*catalog_source;
	const struct mh_material_source		*material_source;
	const struct mh_horn_source		*horn_source;
	const struct mh_env_sound_source	*env_sound_source;
	const struct mh_navigation_source	*navigation_source;
	const struct mh_collision_source	*collision_source;
	const struct mh_image_decoder_provider	*image_decoder;

	/*
	 * A GPU lighting profile supplies this in place of CPU vertex shading.
	 */
	enum mh_error_code (*light_mesh)(const struct mh_race_scene *scene,
		const struct mh_drive *viewed, struct mh_renderer *renderer,
		unsigned int	index, int enabled, struct mh_error *error_out);

	struct mh_race_scene_assets		assets;
	struct mh_race_visuals			visuals;
};

struct mh_scene_effect_slot {
	int	name; /* Zero: stable lamp slot; one: roster nickname slot. */
	size_t	source;
};

enum mh_error_code	mh_race_scene_shade_finish(struct mh_race_scene *scene,
	const struct mh_field *field, const struct mh_finish_frame *frame,
	int reflections, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_finish(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);

enum mh_error_code	mh_race_scene_software_names(
	const struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, unsigned int visible_roots, int enabled,
	struct mh_renderer_name sources[MH_NAME_LABELS_MAX], size_t *count,
	struct mh_error *error_out);

/*
 * _from loaders borrow resources during each call; loaded data is scene-owned.
 * Animation/atlas/image sources and geometry/image decoders are borrowed
 * through scene teardown, including later opponent and finish-sign loading.
 * Pass the same prepared source to later opponent/AI/name/retro loads.
 */
enum mh_error_code	mh_race_scene_load_from(
	const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	struct mh_race_scene *scene, struct mh_error *error_out);

/*
 * Apply ScreenDimension/CarDetail/TrackDetail before loading geometry.
 * Live preferences are supplied separately to frame operations. The viewport,
 * model chain and scenery stay fixed for this asset lifetime; defaults match
 * the ordinary loader. Logical atlas capacity is 4 MiB for Glide, 32 MiB
 * for Direct3D; an explicit texture budget accepts zero through the profile
 * limit. This startup-only capacity does not describe actual GPU memory.
 */
enum mh_error_code	mh_race_scene_load_settings_from(
	const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, struct mh_race_scene *scene,
	struct mh_error *error_out);

/* Profile is fixed for all geometry, effects and subsequently added cars. */
enum mh_error_code	mh_race_scene_load_profile_from(
	const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, enum mh_render_profile profile,
	struct mh_race_scene *scene, struct mh_error *error_out);
void			mh_race_scene_free(struct mh_race_scene *scene);

/*
 * Reports WORLD then TRACK; subsequent car/AI/font stages belong to session
 * setup.
 */
enum mh_error_code	mh_race_scene_load_observed_from(
	const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, enum mh_render_profile profile,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_scene *scene,
	struct mh_error *error_out);

/*
 * Apply the three replacement colors once to this entrant's complete body
 * detail chain before rendering. Separate wheel meshes retain their paint.
 */
enum mh_error_code	mh_race_scene_paint(struct mh_race_scene *scene,
	unsigned int entry, const unsigned long colors[3],
	struct mh_error *error_out);

/*
 * Lazy effect data, required only by r3tr0/a500. Source borrowed for this call.
 */
enum mh_error_code	mh_race_scene_load_retro_from(
	const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);

/* Borrow camera records for each entry; assets must outlive the session. */
void			mh_race_scene_bind_cameras(
	const struct mh_race_scene *scene, struct mh_field *field);
enum mh_error_code	mh_race_scene_bind_objects(
	const struct mh_race_scene *scene, struct mh_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_animate_objects(
	struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_error *error_out);

/*
 * Once per world frame, after particle emission. Shares its random stream.
 * Updates world colors on mode/flash transitions; car/scenery/particle/sky
 * consumers use the same selected style. No simulation clocks advance.
 */
enum mh_error_code	mh_race_scene_weather(struct mh_race_scene *scene,
	const struct mh_field *field, const struct mh_av_settings *settings,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_pause(struct mh_race_scene *scene,
	int dimmed, const struct mh_av_settings *settings,
	struct mh_error *error_out);

/*
 * Select body details for the requested rendering profile from the viewed lens
 * and displayed transforms, and each axle's previously bound wheel variant.
 * Body details 0 and 1 own the separately animated wheels except at Low car
 * quality. Publishes atomically.
 */
enum mh_error_code	mh_race_scene_details(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, unsigned int width,
	enum mh_render_profile profile, struct mh_error *error_out);
int			mh_race_scene_part_visible(
	const struct mh_race_scene *scene, const struct mh_field *field,
	unsigned int mesh);

/*
 * Prepare software model settings after detail/brightness selection. Mesh zero
 * is scenery; bodies and their wheels use the same original car mode. Point
 * lights are selected by the renderer using the body's mesh index as root.
 * Includes track directional/specular inputs and Flat/Gouraud/Reflection;
 * far detail suppresses reflection and selects uniform smooth-face lighting.
 * Output is unchanged on failure. This does not upload or draw geometry.
 */
enum mh_error_code	mh_race_scene_software_lighting(
	const struct mh_race_scene *scene, const struct mh_field *field,
	unsigned int mesh, const struct mh_av_settings *settings,
	struct mh_model_lighting *lighting, struct mh_error *error_out);

/* Shared original object frustum for geometry, names and attached lights. */
enum mh_error_code	mh_race_scene_frustum(const struct mh_field *field,
	unsigned int width, unsigned int height, struct mh_frustum *frustum,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_world_view_profile(
	enum mh_render_profile profile, const struct mh_field *field,
	const struct mh_world *world, unsigned int width, unsigned int height,
	struct mh_world_view *view, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_world_view(const struct mh_field *field,
	const struct mh_world *world, unsigned int width, unsigned int height,
	struct mh_world_view *view, struct mh_error *error_out);

/*
 * Update scenery draw visibility and return the root-sphere mask for cars.
 * Membership/cockpit/LOD gates remain in part_visible. Unavailable bodies
 * retain their ordinary sphere test; shadows have a separate caller.
 */
enum mh_error_code	mh_race_scene_visibility(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, unsigned int width,
	unsigned int height, unsigned int *roots, struct mh_error *error_out);

/* Profile-specific world/scenery/root tests; the ordinary call uses Glide. */
enum mh_error_code	mh_race_scene_visibility_profile(
	enum mh_render_profile profile, struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, unsigned int width,
	unsigned int height, unsigned int *roots, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_prepare_lamps(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_error *error_out);

/*
 * Original shared additive-list order, including cleared slots for hidden
 * LODs/names. Call after lamp reservation. Own/free the returned array.
 * Failure preserves outputs; no lamp history, geometry or roster is changed.
 */
enum mh_error_code	mh_race_scene_order_effects(
	const struct mh_race_scene *scene, unsigned int cars,
	struct mh_scene_effect_slot **output, size_t *count,
	struct mh_error *error_out);

/*
 * The roster array has MH_NAME_LABELS_MAX entries; count must match the field.
 */
enum mh_error_code	mh_race_scene_names_prepare_from(
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, const struct mh_field *field,
	const struct mh_profile roster[MH_NAME_LABELS_MAX], unsigned int count,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_names(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, const float view[16],
	unsigned int width, unsigned int height, int enabled,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_update_lamps(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models,
	const float matrix[16], const float view[16], unsigned int width,
	unsigned int height, const struct mh_av_settings *settings,
	struct mh_error *error_out);

/*
 * Emit once after frame physics; old histories survive recovery/departure.
 * Full race restart clears them. Geometry is rebuilt separately for camera
 * changes.
 */
enum mh_error_code	mh_race_scene_skids(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_error *error_out);
void			mh_race_scene_skids_reset(struct mh_race_scene *scene);
enum mh_error_code	mh_race_scene_load_particles_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_update_particles(
	struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_error *error_out);

/*
 * New assets / explicit diagnostic reset only. In-race restart and recovery
 * retain particle pools, animation cursors, cadence and the shared RNG.
 */
void	mh_race_scene_reset_particles(struct mh_race_scene *scene);

/*
 * Shade one body/wheel using the root car's light cell and this part's pose.
 * glenz enables the diagnostic PC 3.1 environment-face color composition.
 * reflections selects environment materials in the Direct3D lighting
 * diagnostic.
 */
enum mh_error_code	mh_race_scene_shade_car(struct mh_race_scene *scene,
	const struct mh_field *field, unsigned int mesh, const float *models,
	const float view[16], int glenz, int reflections,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_shade_unavailable(
	struct mh_race_scene *scene, const struct mh_field *field,
	unsigned int mesh, const float *models, const float view[16],
	struct mh_error *error_out);
enum mh_error_code	mh_race_scene_load_ai_from(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);
enum mh_error_code	mh_race_scene_add_opponents_from(
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, unsigned int count,
	struct mh_race_scene *scene, struct mh_error *error_out);

/*
 * Explicit catalog choices for entries 1..count; meshes/hulls retain entry
 * order.
 */
enum mh_error_code	mh_race_scene_add_selected_from(
	const struct mh_resource_source *resources, const char *root,
	const unsigned int *cars, unsigned int track, unsigned int count,
	struct mh_race_scene *scene, struct mh_error *error_out);

/* Configure backend lighting from scene policy; no window or session state. */
enum mh_error_code	mh_race_scene_finish_lighting(
	struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_renderer *renderer, unsigned int finish_mesh,
	const struct mh_finish_frame *frame, int reflections,
	struct mh_error *error_out);

#endif

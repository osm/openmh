#ifndef MH_SOFTWARE_RENDERER_INTERNAL_H
#define MH_SOFTWARE_RENDERER_INTERNAL_H

/* Private renderer state and cooperation between backend modules. */
#include "backends/render/software_environment.h"
#include "backends/render/software_face.h"
#include "backends/render/software_fog.h"
#include "backends/render/software_halos.h"
#include "backends/render/software_model_light.h"
#include "backends/render/software_names.h"
#include "backends/render/software_order.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_panorama.h"
#include "backends/render/software_particles.h"
#include "backends/render/software_presentation.h"
#include "backends/render/software_projection.h"
#include "backends/render/software_raster.h"
#include "backends/render/software_skids.h"
#include "backends/render/software_ui.h"
#include "backends/render/software_world_light.h"
#include "engine/render/renderer.h"

#define MH_SOFTWARE_VERTEX_MAX 1000000UL

/* Source data stays borrowed; vertices and lighting caches are owned. */
struct mh_software_mesh {
	const struct mh_mesh		*source;
	struct mh_vertex		*vertices;
	size_t				vertex_count;
	int				visible, shadow;
	struct mh_software_model_light	lighting;
	struct mh_software_world_light	world_light;
	size_t				light_root;
};

struct mh_software_draw_triangle {
	/*
	 * Zero is ordinary geometry; otherwise the halo entry index plus one.
	 */
	size_t				halo;
	struct mh_software_triangle	raster;
	struct mh_software_texture	texture;
	const struct mh_indexed_image	*image;
	int				rejected;
};

/* Native matrix adapter, or explicit original lens with a camera transform. */
struct mh_software_face_view {
	const float				*matrix, *model;
	const struct mh_projection		*projection;
	float					camera[12];
	const struct mh_software_order		*order;
	const struct mh_software_fog		*fog;
	const struct mh_depth_cue		*cue;
	unsigned int				producer_mode;
	const struct mh_mesh_object		*object;
	const struct mh_software_model_light	*lighting;
};

struct mh_software_renderer {
	struct mh_renderer			base;
	struct mh_software_ui			ui;
	struct mh_software_presentation		*presentation;

	/*
	 * Retained display and unpublished frame scratch have separate pixels.
	 */
	struct mh_software_surface		surface;
	struct mh_software_surface		work;
	struct mh_software_panorama		sky;
	struct mh_software_environment		environment;
	struct mh_software_skids		skids;
	struct mh_software_particles		particles;
	struct mh_software_halos		halos;
	struct mh_software_names		names;
	struct mh_software_mesh			*meshes;
	struct mh_renderer_scene_part		*parts;
	size_t	mesh_count, part_count, triangle_capacity;
	size_t	span_capacity, mesh_triangle_capacity, mesh_span_capacity;
	struct mh_software_draw_triangle	*triangles;
	struct mh_software_order_span		*spans;
	struct mh_software_palette		palette;
	struct mh_software_texture_state	texture_state;
	int					frame_ready;

	/* Owned race history and unpublished history scratch; no HUD pixels. */
	unsigned char				*history[2], *history_pending;
	unsigned int				history_next;
	int					motion_blur;
	enum mh_texture_mapping			mapping;
	unsigned int				perspective_shift;
	struct mh_depth_cue			cue;
	struct mh_depth_cue_gradient		gradient;
	int					cue_enabled;
};

/* Mesh admission and cached scenery-object lookup. */
int	mh_software_mesh_supported(const struct mh_mesh *mesh);
int	mh_software_mesh_object(const struct mh_mesh *mesh, size_t primitive,
	struct mh_mesh_object *object);
int	mh_software_mesh_shadow(const struct mh_mesh *mesh);

/* Face conversion and preflight use a copy of retained texture steps. */
int	mh_software_triangle_bind_texture(struct mh_software_renderer *software,
	struct mh_software_draw_triangle *prepared);
int	mh_software_primitive_prepare(struct mh_software_renderer *software,
	const struct mh_software_mesh *mesh,
	const struct mh_mesh_primitive *primitive,
	const struct mh_software_face_view *view, size_t *cursor,
	unsigned int *bucket);
int	mh_software_triangle_validate(struct mh_software_renderer *software,
	struct mh_software_draw_triangle *triangle,
	struct mh_software_texture_state *state, int lens);

/* Build ordered spans without publishing a frame or advancing texture state. */
enum mh_error_code	mh_software_scene_reserve_effects(
	struct mh_software_renderer *software, struct mh_error *error_out);
int			mh_software_scene_prepare(
	struct mh_software_renderer *software, const float matrix[16],
	const float *models, const struct mh_projection *projection,
	const float view[16], const struct mh_depth_cue *cue, size_t *count);

/* Backend frame callbacks; pixels/history commit only after presentation. */
int			mh_software_renderer_scene_frame_matrix(
	struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out);
enum mh_error_code	mh_software_renderer_scene_frame_lens(
	struct mh_renderer *renderer, const struct mh_projection *projection,
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out);

#endif

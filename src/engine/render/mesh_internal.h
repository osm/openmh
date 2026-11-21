#ifndef MH_RENDER_MESH_INTERNAL_H
#define MH_RENDER_MESH_INTERNAL_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"

/* Native validation limits, not limits from the original loader. */
#define MH_MESH_POSITION_LIMIT 1e6
#define MH_MESH_ROTATION_LIMIT 1.000001
#define MH_MESH_TINT_LIMIT 100

/* Owned construction data and staging storage; private to this module. */
struct mh_mesh_binding {
	struct mh_scene_object	source, pose, pending_pose;
	struct mh_mesh_face	*faces;
	size_t			face_count;
	struct mh_vertex	*local_vertices;
	struct mh_vertex	*staged_vertices;
	struct mh_geometry_halo	*halos;
	size_t			halo_count;
	float			radius;
	size_t			vertex_first, scene_index;
	size_t			draw_first, draw_count;
	size_t			primitive_first, primitive_count;
	int			hidden;
	size_t			vertex_count;
	struct mh_mesh_binding	*next;
};

struct mh_mesh_scenery {
	struct mh_mesh_binding	*bindings;
	double			fit_center[3];
	double			fit_radius;
};

struct mh_mesh_world_model {
	float	center[3], radius;
	size_t	draw_first, draw_count;
	size_t	primitive_first;
	int	hidden;
};

struct mh_mesh_world {
	struct mh_mesh_world_model	*models;
	struct mh_world_cell		*cells;
	size_t				model_count, cell_count;
	size_t				width, height;
	struct mh_mesh_span		span;
	size_t				scene_count;
	float				(*planes)[4];
	size_t				plane_count;
	int				native, ordered;
	enum mh_render_profile		visibility_profile;
};

/* Construction helpers preserve primitive order and numeric conversion. */
enum mh_error_code	mh_mesh_init_model(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, size_t faces,
	struct mh_mesh *mesh, struct mh_error *error_out);
enum mh_error_code	mh_mesh_append_model(const struct mh_model *model,
	const double *brightness, enum mh_render_profile profile,
	struct mh_mesh *mesh, struct mh_error *error_out);
enum mh_error_code	mh_mesh_transform_vertices(
	const struct mh_vertex *local, struct mh_vertex *output, size_t count,
	const struct mh_scene_object *pose, const struct mh_mesh_scenery *fit,
	struct mh_error *error_out);
int			mh_mesh_scenery_bounds(
	const struct mh_mesh_scenery *scenery,
	const struct mh_mesh_binding *binding, float position[3],
	float *radius);

#endif

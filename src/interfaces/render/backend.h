#ifndef MH_INTERFACES_RENDER_BACKEND_H
#define MH_INTERFACES_RENDER_BACKEND_H

#include "interfaces/render/renderer.h"

/*
 * Operations are required unless marked optional; there are no implicit effect
 * fallbacks. Boolean operations fill error_out on failure. Dispatch copies the
 * diagnostic before another backend operation can invalidate it and clears it
 * on success.
 */
struct mh_render_ops {
	int (*scene_order)(struct mh_renderer *renderer,
		const struct mh_renderer_scene_part *parts, size_t count,
		struct mh_error			*error_out);

	int (*update_mesh)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);
	int (*mesh_visible)(struct mh_renderer *renderer, size_t index,
		int	visible, struct mh_error *error_out);
	int (*mesh_lit)(struct mh_renderer *renderer, size_t index,
		const struct mh_vertex	*vertices, struct mh_error *error_out);
	int (*mesh_unavailable)(struct mh_renderer *renderer, size_t index,
		const struct mh_vertex	*vertices, struct mh_error *error_out);
	int (*mesh_skids)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);
	int (*mesh_shadows)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);
	int (*mesh_particles)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);
	int (*mesh_sky)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);
	int (*mesh_halos)(struct mh_renderer *renderer, size_t index,
		struct mh_error			*error_out);

	int (*depth_queue)(struct mh_renderer *renderer,
		const struct mh_depth_probe *probes, size_t count,
		struct mh_error			*error_out);
	int (*depth_read)(struct mh_renderer *renderer, unsigned char *visible,
		size_t	count, struct mh_error *error_out);

	int (*set_fog)(struct mh_renderer *renderer, const struct mh_fog *fog,
		struct mh_error			*error_out);
	int (*set_motion_blur)(struct mh_renderer *renderer, int enabled,
		struct mh_error			*error_out);
	int (*set_water)(struct mh_renderer *renderer,
		const struct mh_water_frame	*frame, struct mh_error *error_out);

	int (*ui_upload)(struct mh_renderer *renderer,
		const struct mh_ui_atlas	*atlas, struct mh_error *error_out);

	int (*scene_frame_retro)(struct mh_renderer *renderer,
		const float matrix[16], const float view[16],
		const float *models, int reflections,
		const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
		struct mh_error			*error_out);
	int (*tron_frame_ui)(struct mh_renderer *renderer,
		const struct mh_renderer_tron_face *faces, size_t count,
		const struct mh_ui_list	*ui, struct mh_error *error_out);
	int (*tron_scene_ui)(struct mh_renderer *renderer,
		const struct mh_projection *projection, const float view[16],
		const float *models, int reflections,
		const struct mh_ui_list	*ui, struct mh_error *error_out);

	void	(*destroy)(struct mh_renderer *renderer);

	int (*capture)(struct mh_renderer *renderer, const char *path,
		struct mh_error			*error_out);
	int (*tga)(struct mh_renderer *renderer, struct mh_file *output,
		struct mh_error			*error_out);

	enum mh_error_code (*set_texture_mapping)(struct mh_renderer *renderer,
		enum mh_texture_mapping mapping, unsigned int block_pixels,
		struct mh_error			*error_out);

	enum mh_error_code (*scene_frame_lens)(struct mh_renderer *renderer,
		const struct mh_projection *projection, const float view[16],
		const float *models, int reflections,
		const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
		const struct mh_renderer_effects *effects,
		struct mh_error			*error_out);

	enum mh_error_code (*set_depth_cue)(struct mh_renderer *renderer,
		const struct mh_depth_cue	*cue, struct mh_error *error_out);
	enum mh_error_code (*set_panorama)(struct mh_renderer *renderer,
		const struct mh_panorama	*panorama, struct mh_error *error_out);

	enum mh_error_code (*model_lighting)(struct mh_renderer *renderer,
		size_t index, const struct mh_model_lighting *lighting,
		struct mh_error			*error_out);
	enum mh_error_code (*set_environment)(struct mh_renderer *renderer,
		const struct mh_indexed_image *const maps[3],
		struct mh_error			*error_out);
	enum mh_error_code (*world_lighting)(struct mh_renderer *renderer,
		size_t index, size_t root, const struct mh_world *world,
		const struct mh_model_lighting *lighting,
		struct mh_error			*error_out);
	enum mh_error_code (*mesh_glenz)(struct mh_renderer *renderer,
		size_t index, const struct mh_renderer_glenz *glenz,
		struct mh_error			*error_out);

	enum mh_error_code (*set_texture_filter)(struct mh_renderer *renderer,
		enum mh_texture_filter	filter, struct mh_error *error_out);

	/* Optional window presentation queue control. */
	enum mh_error_code (*set_frame_queue)(struct mh_renderer *renderer,
		unsigned int	frames, struct mh_error *error_out);

	/*
	 * Optional retained color history, independent of presentation queues.
	 */
	enum mh_error_code (*set_blur_history)(struct mh_renderer *renderer,
		unsigned int	frames, struct mh_error *error_out);

	/* Optional owned RGBA8 readback, independent of file encoding. */
	enum mh_error_code (*read_pixels)(struct mh_renderer *renderer,
		struct mh_image	*output, struct mh_error *error_out);

	/* Optional; window presentation only. */
	enum mh_error_code (*set_vsync)(struct mh_renderer *renderer,
		int	enabled, struct mh_error *error_out);
};

struct mh_render_backend {
	const char			*name;
	enum mh_render_profile		profile;

	/* On failure, release partial resources and leave output untouched. */
	enum mh_error_code (*open)(const void *context,
		struct mh_window *window, const struct mh_mesh *const *meshes,
		size_t count, unsigned int width, unsigned int height,
		unsigned int flags, struct mh_renderer **output,
		struct mh_error		*error_out);

	const struct mh_render_ops	*ops;
	const void			*context;
};

/*
 * Host-selected original profiles and the renderer for standalone UI screens.
 * Descriptors/configurations remain borrowed through all dependent renderers.
 * Missing profiles are unavailable; selection never substitutes another
 * profile.
 */
struct mh_render_set {
	const struct mh_render_backend *glide, *software, *direct3d, *powervr,
		*ui, *openmh;
};

const struct mh_render_backend	*mh_render_set_profile(
	const struct mh_render_set *renderers, enum mh_render_profile profile);

#endif

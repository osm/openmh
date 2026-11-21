#ifndef MH_WEBGL_RENDERER_H
#define MH_WEBGL_RENDERER_H

#include "backends/render/webgl_context.h"
#include "backends/render/webgl_depth.h"
#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_history.h"
#include "backends/render/webgl_retro.h"
#include "backends/render/webgl_tron.h"
#include "backends/render/webgl_ui.h"
#include "engine/render/renderer.h"
#include "engine/render/scene_tron.h"

struct mh_webgl_renderer {
	struct mh_renderer		base;
	struct mh_webgl_context		context;
	struct mh_webgl_scene		*scene;
	struct mh_webgl_drawing		drawing;
	struct mh_webgl_materials	materials;
	struct mh_webgl_history		history;
	struct mh_webgl_depth		depth;
	struct mh_webgl_retro		retro;
	struct mh_webgl_tron		tron;
	struct mh_tron_scene		tron_scene;
	struct mh_webgl_ui		*ui;
	struct mh_fog			fog;
	struct mh_water_frame		water;
	unsigned int			width, height;
	int				fog_enabled, water_enabled;

	/* Retain history settings if an interrupted rebuild must be retried. */
	int				restoring, restore_blur;
	unsigned int			restore_frames;
	char				diagnostic[1024];
};

enum mh_error_code	mh_webgl_renderer_open(struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, int powervr,
	struct mh_renderer **output, struct mh_error *error_out);
void			mh_webgl_renderer_destroy(struct mh_renderer *renderer);
enum mh_error_code	mh_webgl_renderer_restore(void *context,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_ui(struct mh_webgl_renderer *renderer,
	const struct mh_ui_atlas *atlas, struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_frame(
	struct mh_webgl_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_projection *projection, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro, struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_pixels(struct mh_renderer *renderer,
	struct mh_image *output, struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_tron_frame(
	struct mh_webgl_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out);
enum mh_error_code	mh_webgl_renderer_tron_scene(
	struct mh_webgl_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	struct mh_error *error_out);
int			mh_webgl_renderer_capture(struct mh_renderer *renderer,
	const char *path, struct mh_error *error_out);
int			mh_webgl_renderer_tga(struct mh_renderer *renderer,
	struct mh_file *output, struct mh_error *error_out);

#endif

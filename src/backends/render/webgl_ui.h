#ifndef MH_WEBGL_UI_H
#define MH_WEBGL_UI_H

#include "backends/render/webgl_program.h"
#include "backends/render/webgl_target.h"
#include "backends/render/webgl_vertex_array.h"
#include "engine/ui/atlas.h"
#include "engine/ui/draw.h"

struct mh_webgl_ui {
	struct mh_webgl_program		program;
	struct mh_webgl_texture		atlas;
	struct mh_image			source;
	struct mh_webgl_vertex_array	array;
	GLuint				vertices, viewport, sampler;
	unsigned int			width, height;
};

/*
 * Zero-initialize output. Owns a copy of atlas pixels, program and buffers.
 * Failed creation releases partial resources; shader diagnostics borrow output
 * until its next create call. Keep the owning context current for all calls.
 */
enum mh_error_code	mh_webgl_ui_create(struct mh_webgl_ui *output,
	const struct mh_ui_atlas *atlas, unsigned int width,
	unsigned int height, struct mh_error *error_out);

/*
 * Draw over existing color without changing depth; owns draw state for this
 * pass.
 */
enum mh_error_code	mh_webgl_ui_draw(struct mh_webgl_ui *ui,
	const struct mh_ui_list *list, const struct mh_webgl_target *target,
	struct mh_error *error_out);
void			mh_webgl_ui_destroy(struct mh_webgl_ui *ui);

/* Retain copied atlas pixels and dimensions across GPU resource replacement. */
void			mh_webgl_ui_release(struct mh_webgl_ui *ui);
enum mh_error_code	mh_webgl_ui_restore(struct mh_webgl_ui *ui,
	struct mh_error *error_out);

#endif

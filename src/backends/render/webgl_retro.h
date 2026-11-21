#ifndef MH_WEBGL_RETRO_H
#define MH_WEBGL_RETRO_H

#include "backends/render/webgl_ui.h"
#include "engine/ui/retro.h"

struct mh_webgl_retro {
	struct mh_webgl_program		program;
	struct mh_webgl_vertex_array	array;
	GLuint				vertices, viewport, material;
	unsigned int			count, text_end, width, height;
};

/*
 * Zero-initialize storage; keep the owning context current for all calls.
 * Prepare lazily creates resources and copies a validated frame. Diagnostics
 * borrow this owner's program storage until its next creation attempt.
 * Draw borrows the current HUD atlas and preserves existing color/depth.
 */
enum mh_error_code	mh_webgl_retro_prepare(struct mh_webgl_retro *retro,
	const struct mh_retro_draw *draw, struct mh_error *error_out);
enum mh_error_code	mh_webgl_retro_draw(const struct mh_webgl_retro *retro,
	const struct mh_webgl_ui *ui, const struct mh_webgl_target *target,
	struct mh_error *error_out);
void			mh_webgl_retro_destroy(struct mh_webgl_retro *retro);

#endif

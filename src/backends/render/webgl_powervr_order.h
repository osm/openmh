#ifndef MH_WEBGL_POWERVR_ORDER_H
#define MH_WEBGL_POWERVR_ORDER_H

#include "backends/render/webgl_draw.h"
#include "backends/render/webgl_materials.h"

struct mh_webgl_order;

/* Reuse CPU storage sized for this frame; calls borrow GPU resources. */
enum mh_error_code	mh_webgl_order_begin(struct mh_webgl_order **output,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials, unsigned int width,
	unsigned int height, struct mh_error *error_out);
enum mh_error_code	mh_webgl_order_defer(struct mh_webgl_order *order,
	const struct mh_webgl_draw_call *call, const struct mh_webgl_mesh *mesh,
	const struct mh_webgl_materials *materials, int prepared,
	struct mh_error *error_out);

/* Draw sorted regions, restoring the caller's scissor rectangle and enable. */
enum mh_error_code	mh_webgl_order_draw(struct mh_webgl_order *order,
	struct mh_webgl_drawing *drawing, struct mh_error *error_out);
void			mh_webgl_order_destroy(struct mh_webgl_order *order);

#endif

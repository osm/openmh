#ifndef MH_WEBGL_TRON_H
#define MH_WEBGL_TRON_H

#include "backends/render/webgl_program.h"
#include "backends/render/webgl_scene.h"
#include "backends/render/webgl_target.h"
#include "engine/render/tron_draw.h"

struct mh_webgl_tron_draw;

struct mh_webgl_tron {
	struct mh_webgl_program		program;
	struct mh_webgl_vertex_array	array;
	GLuint				vertices, viewport, material, sampler;
	struct mh_tron_vertex		*upload;
	struct mh_webgl_tron_draw	*draws;
	size_t				capacity, count;
	unsigned int			width, height;
};

/*
 * Zero-initialize storage and keep its context current. Prepare copies faces
 * into owned vertices/draw ranges and borrows scene texture handles. A failed
 * prepare must not be drawn. Program diagnostics borrow this owner's storage.
 */
enum mh_error_code	mh_webgl_tron_prepare(struct mh_webgl_tron *tron,
	const struct mh_webgl_scene *scene,
	const struct mh_renderer_tron_face *faces, size_t count,
	unsigned int width, unsigned int height, struct mh_error *error_out);
enum mh_error_code	mh_webgl_tron_draw(const struct mh_webgl_tron *tron,
	const struct mh_webgl_target *target, struct mh_error *error_out);
void			mh_webgl_tron_destroy(struct mh_webgl_tron *tron);

#endif

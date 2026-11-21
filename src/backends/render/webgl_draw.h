#ifndef MH_WEBGL_DRAW_H
#define MH_WEBGL_DRAW_H

#include "backends/render/shader_uniforms.h"
#include "backends/render/webgl_geometry.h"
#include "backends/render/webgl_program.h"
#include "engine/render/pipeline.h"
#include "interfaces/render/renderer.h"

struct mh_webgl_draw_call {
	const struct mh_webgl_geometry	*geometry;
	size_t				first, count;
	GLuint				texture, sampler;
	enum mh_webgl_program_kind	program;
	enum mh_gpu_pipeline		pipeline;
	float	camera[MH_GPU_CAMERA_FLOATS], material[MH_GPU_MATERIAL_FLOATS];
	const float			*lighting;
	float				surface[4];
};

struct mh_webgl_drawing {
	struct mh_webgl_program		programs[MH_WEBGL_PROGRAM_COUNT];
	GLuint				blocks[4];
	GLuint				samplers[3][2], sky_sampler;
	enum mh_texture_filter		filter;
	struct mh_webgl_order		*order;
	struct mh_webgl_draw_call	bound;
	int				bound_valid;
};

/*
 * Zero-initialize; keep the context current. Diagnostics borrow this storage.
 */
enum mh_error_code	mh_webgl_drawing_create(
	struct mh_webgl_drawing *drawing, struct mh_error *error_out);
enum mh_error_code	mh_webgl_drawing_openmh(
	struct mh_webgl_drawing *drawing, struct mh_error *error_out);
void			mh_webgl_drawing_destroy(
	struct mh_webgl_drawing *drawing);

/*
 * Bind complete scene shader/raster/resource state; retain target and viewport.
 * The enclosing pass resets bound_valid after external draw state changes.
 * Driver errors are collected by the enclosing scene pass, not per primitive.
 */
enum mh_error_code	mh_webgl_draw_bind(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_draw_call *call, struct mh_error *error_out);

/* Bind the call and draw its complete triangle range. */
enum mh_error_code	mh_webgl_draw_submit(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_draw_call *call, struct mh_error *error_out);
GLuint			mh_webgl_material_sampler(
	const struct mh_webgl_drawing *drawing, int powervr, int direct3d,
	int glenz, int environment, int alpha);

#endif

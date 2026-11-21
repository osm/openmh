#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_context.h"
#include "backends/render/webgl_error.h"
#include "backends/render/webgl_presentation.h"
#include "backends/render/webgl_program.h"
#include "backends/render/webgl_provider.h"
#include "backends/render/webgl_sampler.h"
#include "backends/render/webgl_vertex_array.h"
#include "engine/ui/draw.h"
#include "interfaces/render/canvas_backend.h"

struct webgl_canvas {
	struct mh_webgl_context		context;
	struct mh_webgl_program		program;
	struct mh_webgl_texture		image;
	struct mh_webgl_target		target;
	struct mh_webgl_vertex_array	array;
	struct mh_canvas_layout		layout;
	GLuint				vertices, viewport, sampler;
};

static void
destroy_resources(struct webgl_canvas *canvas)
{
	glUseProgram(0);
	mh_webgl_program_destroy(&canvas->program);
	mh_webgl_texture_destroy(&canvas->image);
	mh_webgl_target_destroy(&canvas->target);
	mh_webgl_vertex_array_destroy(&canvas->array);
	glDeleteBuffers(1, &canvas->vertices);
	glDeleteBuffers(1, &canvas->viewport);
	glDeleteSamplers(1, &canvas->sampler);
	canvas->vertices = canvas->viewport = canvas->sampler = 0;
}

static void
close_canvas(void *context, void *token)
{
	struct webgl_canvas *canvas;
	enum mh_error_code status;

	(void)context;
	canvas = token;

	if (canvas == NULL)
		return;

	if (canvas->context.handle != 0) {
		canvas->context.restore = NULL;
		status = mh_webgl_context_current(&canvas->context, NULL);
		destroy_resources(canvas);

		if (status != MH_ERROR_NONE)
			mh_webgl_discard_lost_object_errors();

		mh_webgl_context_close(&canvas->context);
	}

	free(canvas);
}

static enum mh_error_code
create_buffers(struct webgl_canvas *canvas, struct mh_error *error_out)
{
	size_t offset;
	enum mh_error_code status;

	glGenBuffers(1, &canvas->vertices);
	glBindBuffer(GL_ARRAY_BUFFER, canvas->vertices);
	glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(struct mh_ui_vertex), NULL,
		GL_STREAM_DRAW);
	glGenBuffers(1, &canvas->viewport);
	glBindBuffer(GL_UNIFORM_BUFFER, canvas->viewport);
	glBufferData(
		GL_UNIFORM_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_DRAW);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	offset = 0;

	return mh_webgl_vertex_array_create(&canvas->array, MH_GPU_UI_VERTEX,
		&canvas->vertices, &offset, error_out);
}

static enum mh_error_code
create_resources(struct webgl_canvas *canvas, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_webgl_program_create(
		&canvas->program, MH_WEBGL_UI, error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"cannot create WebGL canvas shader program");

	status = mh_webgl_texture_create(&canvas->image, canvas->layout.width,
		canvas->layout.height, 1, GL_RGBA8, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_sampler_create(&canvas->sampler,
			MH_TEXTURE_FILTER_NEAREST, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = create_buffers(canvas, error_out);

	return status;
}

/* Canvas pixels are supplied afresh by the next menu/movie/software frame. */
static enum mh_error_code
restore_canvas(void *context, struct mh_error *error_out)
{
	struct webgl_canvas *canvas;

	canvas = context;
	destroy_resources(canvas);
	mh_webgl_discard_lost_object_errors();

	return create_resources(canvas, error_out);
}

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct webgl_canvas *canvas;
	enum mh_error_code status;

	(void)context;
	canvas = calloc(1, sizeof(*canvas));

	if (canvas == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL canvas");

	canvas->layout = *layout;
	status = mh_webgl_context_open(&canvas->context, window, error_out);

	if (status == MH_ERROR_NONE)
		status = create_resources(canvas, error_out);

	if (status != MH_ERROR_NONE) {
		close_canvas(NULL, canvas);
		return status;
	}

	canvas->context.restore = restore_canvas;
	canvas->context.restore_context = canvas;
	*output = canvas;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resize_target(struct webgl_canvas *canvas, struct mh_error *error_out)
{
	struct mh_webgl_target target;
	unsigned int width, height;
	enum mh_error_code status;

	status = mh_webgl_context_size(
		&canvas->context, &width, &height, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (canvas->target.color.width == width &&
		canvas->target.color.height == height)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&target, 0, sizeof(target));
	status = mh_webgl_target_create(&target, width, height, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_webgl_target_destroy(&canvas->target);
	canvas->target = target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
image_vertices(
	const struct webgl_canvas *canvas, struct mh_ui_vertex vertices[6])
{
	static const unsigned int corners[6][2] = {
		{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};

	const struct mh_canvas_layout *layout;
	float width, height, screen_width, screen_height, x, y, scale_x,
		scale_y;
	unsigned int i, c;

	layout = &canvas->layout;
	width = (float)canvas->target.color.width;
	height = (float)canvas->target.color.height;

	/*
	 * Retain SDL's whole-pixel fitted screen size and half-pixel centering.
	 */
	if ((float)layout->screen_width / layout->screen_height >
		width / height) {
		screen_width = width;
		screen_height = (float)floor((float)layout->screen_height *
			(width / layout->screen_width));
	} else {
		screen_height = height;
		screen_width = (float)floor((float)layout->screen_width *
			(height / layout->screen_height));
	}

	scale_x = screen_width / layout->screen_width;
	scale_y = screen_height / layout->screen_height;
	x = (width - screen_width) * 0.5f + layout->x * scale_x;
	y = (height - screen_height) * 0.5f + layout->y * scale_y;

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] =
			x + corners[i][0] * layout->width * scale_x;
		vertices[i].position[1] =
			y + corners[i][1] * layout->height * scale_y;
		vertices[i].uv[0] = (float)corners[i][0];
		vertices[i].uv[1] = (float)corners[i][1];

		for (c = 0; c < 4; c++)
			vertices[i].color[c] = 1;
	}
}

static void
draw_image(const struct webgl_canvas *canvas)
{
	struct mh_ui_vertex vertices[6];
	float viewport[4];

	image_vertices(canvas, vertices);
	viewport[0] = (float)canvas->target.color.width;
	viewport[1] = (float)canvas->target.color.height;
	viewport[2] = viewport[3] = 0;
	glBindBuffer(GL_ARRAY_BUFFER, canvas->vertices);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glBindBuffer(GL_UNIFORM_BUFFER, canvas->viewport);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(viewport), viewport);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, canvas->target.framebuffer);
	glViewport(0, 0, (GLsizei)canvas->target.color.width,
		(GLsizei)canvas->target.color.height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(canvas->program.handle);
	glBindVertexArray(canvas->array.handle);
	glBindBufferBase(GL_UNIFORM_BUFFER, MH_WEBGL_VERTEX_PARAMETERS,
		canvas->viewport);
	glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
	glBindTexture(GL_TEXTURE_2D, canvas->image.handle);
	glBindSampler(MH_WEBGL_TEXTURE, canvas->sampler);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

static enum mh_error_code
draw(void *context, void *token, const unsigned char *pixels,
	struct mh_error *error_out)
{
	struct webgl_canvas *canvas;
	struct mh_image image;
	enum mh_error_code status;

	(void)context;
	canvas = token;
	status = mh_webgl_context_current(&canvas->context, error_out);

	if (status == MH_ERROR_NONE)
		status = resize_target(canvas, error_out);

	image.width = canvas->layout.width;
	image.height = canvas->layout.height;
	image.rgba = (unsigned char *)pixels;

	if (status == MH_ERROR_NONE)
		status = mh_webgl_texture_upload(
			&canvas->image, 0, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	draw_image(canvas);

	return mh_webgl_check_error(error_out);
}

static enum mh_error_code
present(void *context, void *token, struct mh_error *error_out)
{
	struct webgl_canvas *canvas;
	unsigned int width, height;
	enum mh_error_code status;

	(void)context;
	canvas = token;
	status = mh_webgl_context_current(&canvas->context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_context_size(
			&canvas->context, &width, &height, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_present(&canvas->target, width, height, error_out);
}

static enum mh_error_code
read_pixels(void *context, void *token, struct mh_image *output,
	struct mh_error *error_out)
{
	struct webgl_canvas *canvas;
	enum mh_error_code status;

	(void)context;
	canvas = token;
	status = mh_webgl_context_current(&canvas->context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_read_pixels(&canvas->target, output, error_out);
}

const struct mh_canvas_provider *
mh_webgl_canvas_provider(void)
{
	static const struct mh_canvas_ops ops = {
		open_canvas, close_canvas, draw, present, read_pixels, NULL};

	static const struct mh_canvas_provider provider = {&ops, NULL};

	return &provider;
}

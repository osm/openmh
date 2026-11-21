#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_sampler.h"
#include "backends/render/webgl_ui.h"
#include "engine/render/viewport.h"

enum {
	ATLAS_LIMIT = 2048,
	QUAD_VERTICES = 6
};

static enum mh_error_code
create_buffers(struct mh_webgl_ui *ui, unsigned int width, unsigned int height,
	struct mh_error *error_out)
{
	float viewport[4];
	size_t offset;
	enum mh_error_code status;

	viewport[0] = (float)width;
	viewport[1] = (float)height;
	viewport[2] = viewport[3] = 0;
	glGenBuffers(1, &ui->viewport);
	glBindBuffer(GL_UNIFORM_BUFFER, ui->viewport);
	glBufferData(
		GL_UNIFORM_BUFFER, sizeof(viewport), viewport, GL_STATIC_DRAW);
	glGenBuffers(1, &ui->vertices);
	glBindBuffer(GL_ARRAY_BUFFER, ui->vertices);
	glBufferData(GL_ARRAY_BUFFER,
		MH_UI_MAX_QUADS * QUAD_VERTICES * sizeof(struct mh_ui_vertex),
		NULL, GL_STREAM_DRAW);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	offset = 0;

	return mh_webgl_vertex_array_create(&ui->array, MH_GPU_UI_VERTEX,
		&ui->vertices, &offset, error_out);
}

enum mh_error_code
mh_webgl_ui_restore(struct mh_webgl_ui *ui, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_webgl_program_create(&ui->program, MH_WEBGL_UI, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_texture_from_image(
			&ui->atlas, &ui->source, 0, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_sampler_create(&ui->sampler,
			MH_TEXTURE_FILTER_NEAREST, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = create_buffers(ui, ui->width, ui->height, error_out);

	return status;
}

enum mh_error_code
mh_webgl_ui_create(struct mh_webgl_ui *output, const struct mh_ui_atlas *atlas,
	unsigned int width, unsigned int height, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t bytes;

	if (output == NULL || atlas == NULL || atlas->image.rgba == NULL ||
		atlas->image.width == 0 || atlas->image.height == 0 ||
		atlas->image.width > ATLAS_LIMIT ||
		atlas->image.height > ATLAS_LIMIT || width == 0 ||
		height == 0 || width > MH_VIEW_MAX_WIDTH ||
		height > MH_VIEW_MAX_HEIGHT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL UI atlas or dimensions");

	if (output->program.handle != 0 || output->source.rgba != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL UI is already created");

	bytes = (size_t)atlas->image.width * atlas->image.height * 4;
	output->source.rgba = malloc(bytes);

	if (output->source.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot retain WebGL UI atlas pixels");

	memcpy(output->source.rgba, atlas->image.rgba, bytes);
	output->source.width = atlas->image.width;
	output->source.height = atlas->image.height;
	output->width = width;
	output->height = height;
	status = mh_webgl_ui_restore(output, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_ui_destroy(output);
		return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
draw_state(const struct mh_webgl_ui *ui, const struct mh_webgl_target *target)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
	glViewport(0, 0, (GLsizei)ui->width, (GLsizei)ui->height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
		GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(ui->program.handle);
	glBindVertexArray(ui->array.handle);
	glBindBufferBase(
		GL_UNIFORM_BUFFER, MH_WEBGL_VERTEX_PARAMETERS, ui->viewport);
	glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
	glBindTexture(GL_TEXTURE_2D, ui->atlas.handle);
	glBindSampler(MH_WEBGL_TEXTURE, ui->sampler);
}

enum mh_error_code
mh_webgl_ui_draw(struct mh_webgl_ui *ui, const struct mh_ui_list *list,
	const struct mh_webgl_target *target, struct mh_error *error_out)
{
	if (ui == NULL || list == NULL || target == NULL ||
		target->framebuffer == 0 || ui->program.handle == 0 ||
		list->count > MH_UI_MAX_QUADS * QUAD_VERTICES ||
		list->count % QUAD_VERTICES != 0 || list->width != ui->width ||
		list->height != ui->height ||
		target->color.width != ui->width ||
		target->color.height != ui->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL UI frame or target");

	if (list->text_count != 0 || list->text_size != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"word-font UI commands require the software profile");

	if (list->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	glBindBuffer(GL_ARRAY_BUFFER, ui->vertices);
	glBufferSubData(GL_ARRAY_BUFFER, 0,
		list->count * sizeof(struct mh_ui_vertex), list->vertices);
	draw_state(ui, target);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)list->count);

	return mh_webgl_check_error(error_out);
}

void
mh_webgl_ui_release(struct mh_webgl_ui *ui)
{
	GLint active;

	if (ui == NULL)
		return;

	active = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &active);

	if ((GLuint)active == ui->program.handle)
		glUseProgram(0);

	mh_webgl_program_destroy(&ui->program);
	mh_webgl_vertex_array_destroy(&ui->array);
	mh_webgl_texture_destroy(&ui->atlas);
	glDeleteBuffers(1, &ui->vertices);
	glDeleteBuffers(1, &ui->viewport);
	glDeleteSamplers(1, &ui->sampler);
	ui->vertices = ui->viewport = ui->sampler = 0;
}

void
mh_webgl_ui_destroy(struct mh_webgl_ui *ui)
{
	if (ui == NULL)
		return;

	mh_webgl_ui_release(ui);
	free(ui->source.rgba);
	memset(&ui->source, 0, sizeof(ui->source));
	ui->width = ui->height = 0;
}

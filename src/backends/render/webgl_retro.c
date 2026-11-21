#include "backends/render/webgl_error.h"
#include "backends/render/webgl_retro.h"

void
mh_webgl_retro_destroy(struct mh_webgl_retro *retro)
{
	GLint active;

	if (retro == NULL)
		return;

	active = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &active);

	if ((GLuint)active == retro->program.handle)
		glUseProgram(0);

	mh_webgl_vertex_array_destroy(&retro->array);
	mh_webgl_program_destroy(&retro->program);
	glDeleteBuffers(1, &retro->vertices);
	glDeleteBuffers(1, &retro->viewport);
	glDeleteBuffers(1, &retro->material);
	retro->vertices = retro->viewport = retro->material = 0;
	retro->count = retro->text_end = retro->width = retro->height = 0;
}

static enum mh_error_code
create(struct mh_webgl_retro *retro, struct mh_error *error_out)
{
	size_t offset;
	enum mh_error_code status;

	status = mh_webgl_program_create(
		&retro->program, MH_WEBGL_RETRO, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	glGenBuffers(1, &retro->viewport);
	glBindBuffer(GL_UNIFORM_BUFFER, retro->viewport);
	glBufferData(
		GL_UNIFORM_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_DRAW);
	glGenBuffers(1, &retro->material);
	glBindBuffer(GL_UNIFORM_BUFFER, retro->material);
	glBufferData(
		GL_UNIFORM_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_DRAW);
	glGenBuffers(1, &retro->vertices);
	glBindBuffer(GL_ARRAY_BUFFER, retro->vertices);
	glBufferData(GL_ARRAY_BUFFER,
		MH_RETRO_MAX_VERTICES * sizeof(struct mh_retro_draw_vertex),
		NULL, GL_STREAM_DRAW);
	status = mh_webgl_check_error(error_out);
	offset = 0;

	if (status == MH_ERROR_NONE)
		status = mh_webgl_vertex_array_create(&retro->array,
			MH_GPU_RETRO_VERTEX, &retro->vertices, &offset,
			error_out);

	if (status != MH_ERROR_NONE)
		mh_webgl_retro_destroy(retro);

	return status;
}

enum mh_error_code
mh_webgl_retro_prepare(struct mh_webgl_retro *retro,
	const struct mh_retro_draw *draw, struct mh_error *error_out)
{
	float viewport[4];
	enum mh_error_code status;

	if (retro == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL retro renderer");

	status = mh_retro_draw_validate(draw, error_out);

	if (status == MH_ERROR_NONE && retro->program.handle == 0)
		status = create(retro, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	retro->count = 0;
	glBindBuffer(GL_ARRAY_BUFFER, retro->vertices);
	glBufferSubData(GL_ARRAY_BUFFER, 0,
		draw->count * sizeof(*draw->vertices), draw->vertices);
	viewport[0] = (float)draw->width;
	viewport[1] = (float)draw->height;
	viewport[2] = viewport[3] = 0;
	glBindBuffer(GL_UNIFORM_BUFFER, retro->viewport);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(viewport), viewport);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	retro->count = draw->count;
	retro->text_end = draw->text_end;
	retro->width = draw->width;
	retro->height = draw->height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
draw_state(const struct mh_webgl_retro *retro, const struct mh_webgl_ui *ui,
	const struct mh_webgl_target *target)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
	glViewport(0, 0, (GLsizei)retro->width, (GLsizei)retro->height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthRangef(0, 1);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	glUseProgram(retro->program.handle);
	glBindVertexArray(retro->array.handle);
	glBindBufferBase(
		GL_UNIFORM_BUFFER, MH_WEBGL_VERTEX_PARAMETERS, retro->viewport);
	glBindBufferBase(
		GL_UNIFORM_BUFFER, MH_WEBGL_FRAGMENT_MATERIAL, retro->material);
	glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
	glBindTexture(GL_TEXTURE_2D, ui->atlas.handle);
	glBindSampler(MH_WEBGL_TEXTURE, ui->sampler);
}

static void
draw_range(const struct mh_webgl_retro *retro, unsigned int first,
	unsigned int count, int textured)
{
	float material[4];

	if (count == 0)
		return;

	material[0] = (float)textured;
	material[1] = material[2] = material[3] = 0;
	glBindBuffer(GL_UNIFORM_BUFFER, retro->material);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(material), material);
	glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
}

enum mh_error_code
mh_webgl_retro_draw(const struct mh_webgl_retro *retro,
	const struct mh_webgl_ui *ui, const struct mh_webgl_target *target,
	struct mh_error *error_out)
{
	if (retro == NULL || retro->count == 0 || ui == NULL ||
		ui->atlas.handle == 0 || ui->sampler == 0 || target == NULL ||
		target->framebuffer == 0 || target->depth.handle == 0 ||
		target->color.width != retro->width ||
		target->color.height != retro->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL retro frame, target or font atlas");

	draw_state(retro, ui, target);

	/*
	 * The backdrop replaces depth even where the existing world is nearer.
	 */
	glDepthFunc(GL_ALWAYS);
	draw_range(retro, 0, MH_RETRO_BACKDROP_VERTICES, 0);
	glDepthFunc(GL_LEQUAL);
	draw_range(retro, MH_RETRO_BACKDROP_VERTICES,
		retro->text_end - MH_RETRO_BACKDROP_VERTICES, 1);
	draw_range(retro, retro->text_end, retro->count - retro->text_end, 0);

	return mh_webgl_check_error(error_out);
}

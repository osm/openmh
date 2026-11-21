#include <stdlib.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_sampler.h"
#include "backends/render/webgl_tron.h"

struct mh_webgl_tron_draw {
	struct mh_tron_draw	range;
	GLuint			texture;
};

void
mh_webgl_tron_destroy(struct mh_webgl_tron *tron)
{
	GLint active;

	if (tron == NULL)
		return;

	active = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &active);

	if ((GLuint)active == tron->program.handle)
		glUseProgram(0);

	mh_webgl_vertex_array_destroy(&tron->array);
	mh_webgl_program_destroy(&tron->program);
	glDeleteBuffers(1, &tron->vertices);
	glDeleteBuffers(1, &tron->viewport);
	glDeleteBuffers(1, &tron->material);
	glDeleteSamplers(1, &tron->sampler);
	free(tron->upload);
	free(tron->draws);
	tron->upload = NULL;
	tron->draws = NULL;
	tron->vertices = tron->viewport = tron->material = tron->sampler = 0;
	tron->capacity = tron->count = 0;
	tron->width = tron->height = 0;
}

static enum mh_error_code
create(struct mh_webgl_tron *tron, struct mh_error *error_out)
{
	size_t offset;
	enum mh_error_code status;

	status = mh_webgl_program_create(
		&tron->program, MH_WEBGL_TRON, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	glGenBuffers(1, &tron->viewport);
	glBindBuffer(GL_UNIFORM_BUFFER, tron->viewport);
	glBufferData(
		GL_UNIFORM_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_DRAW);
	glGenBuffers(1, &tron->material);
	glBindBuffer(GL_UNIFORM_BUFFER, tron->material);
	glBufferData(
		GL_UNIFORM_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_DRAW);
	glGenBuffers(1, &tron->vertices);
	glBindBuffer(GL_ARRAY_BUFFER, tron->vertices);
	status = mh_webgl_check_error(error_out);
	offset = 0;

	if (status == MH_ERROR_NONE)
		status = mh_webgl_vertex_array_create(&tron->array,
			MH_GPU_TRON_VERTEX, &tron->vertices, &offset,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_sampler_create(&tron->sampler,
			MH_TEXTURE_FILTER_NEAREST, 1, 1, error_out);

	if (status != MH_ERROR_NONE)
		mh_webgl_tron_destroy(tron);

	return status;
}

static enum mh_error_code
reserve(struct mh_webgl_tron *tron, size_t count, struct mh_error *error_out)
{
	struct mh_webgl_tron_draw *draws;
	struct mh_tron_vertex *vertices;
	size_t bytes;
	enum mh_error_code status;

	if (count <= tron->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	bytes = count * MH_TRON_FACE_VERTICES_MAX * sizeof(*vertices);
	draws = malloc(count * sizeof(*draws));
	vertices = malloc(bytes);

	if (draws == NULL || vertices == NULL) {
		free(draws);
		free(vertices);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL TRON frame");
	}

	glBindBuffer(GL_ARRAY_BUFFER, tron->vertices);
	glBufferData(GL_ARRAY_BUFFER, bytes, NULL, GL_STREAM_DRAW);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE) {
		free(draws);
		free(vertices);
		return status;
	}

	free(tron->draws);
	free(tron->upload);
	tron->draws = draws;
	tron->upload = vertices;
	tron->capacity = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
face_texture(const struct mh_webgl_scene *scene,
	const struct mh_renderer_tron_face *face, GLuint *output,
	struct mh_error *error_out)
{
	const struct mh_webgl_mesh *mesh;
	size_t image;

	if (face->model >= scene->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL TRON model");

	mesh = &scene->meshes[face->model];

	if (face->texture >= mesh->source->texture_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL TRON texture");

	image = mesh->images[face->texture];

	if (image >= scene->images.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL TRON texture");

	*output = scene->images.items[image].texture.handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_faces(struct mh_webgl_tron *tron, const struct mh_webgl_scene *scene,
	const struct mh_renderer_tron_face *faces, size_t count, size_t *used,
	struct mh_error *error_out)
{
	struct mh_webgl_tron_draw *draw;
	size_t i;
	enum mh_error_code status;

	*used = 0;

	for (i = 0; i < count; i++) {
		draw = &tron->draws[i];
		status = face_texture(
			scene, &faces[i], &draw->texture, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_tron_draw_face(&faces[i], tron->width,
				tron->height, &draw->range,
				tron->upload + *used, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		draw->range.fill_first += (unsigned int)*used;
		draw->range.outline_first += (unsigned int)*used;
		*used += draw->range.fill_count + draw->range.outline_count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_tron_prepare(struct mh_webgl_tron *tron,
	const struct mh_webgl_scene *scene,
	const struct mh_renderer_tron_face *faces, size_t count,
	unsigned int width, unsigned int height, struct mh_error *error_out)
{
	float viewport[4];
	size_t used;
	enum mh_error_code status;

	if (tron == NULL || scene == NULL || count > MH_TRON_MAX_FACES ||
		(count != 0 && faces == NULL) || width == 0 || width > 16384 ||
		height == 0 || height > 16384)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL TRON frame");

	tron->count = 0;
	tron->width = width;
	tron->height = height;

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (tron->program.handle == 0) {
		status = create(tron, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = reserve(tron, count, error_out);

	if (status == MH_ERROR_NONE)
		status = prepare_faces(
			tron, scene, faces, count, &used, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	glBindBuffer(GL_ARRAY_BUFFER, tron->vertices);
	glBufferSubData(
		GL_ARRAY_BUFFER, 0, used * sizeof(*tron->upload), tron->upload);
	viewport[0] = (float)width;
	viewport[1] = (float)height;
	viewport[2] = viewport[3] = 0;
	glBindBuffer(GL_UNIFORM_BUFFER, tron->viewport);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(viewport), viewport);
	status = mh_webgl_check_error(error_out);

	if (status == MH_ERROR_NONE)
		tron->count = count;

	return status;
}

static void
draw_state(
	const struct mh_webgl_tron *tron, const struct mh_webgl_target *target)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);

	glViewport(0, 0, (GLsizei)tron->width, (GLsizei)tron->height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glDepthRangef(0, 1);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glLineWidth(1);
	glUseProgram(tron->program.handle);
	glBindVertexArray(tron->array.handle);
	glBindBufferBase(
		GL_UNIFORM_BUFFER, MH_WEBGL_VERTEX_PARAMETERS, tron->viewport);
	glBindBufferBase(
		GL_UNIFORM_BUFFER, MH_WEBGL_FRAGMENT_MATERIAL, tron->material);
	glBindBuffer(GL_UNIFORM_BUFFER, tron->material);
	glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
	glBindSampler(MH_WEBGL_TEXTURE, tron->sampler);
}

static void
blend(enum mh_tron_blend mode)
{
	GLenum source, destination;

	source = mode == MH_TRON_BACKGROUND    ? GL_ONE_MINUS_SRC_ALPHA
		: mode == MH_TRON_SOURCE_ALPHA ? GL_SRC_ALPHA
					: GL_ONE;
	destination = mode == MH_TRON_BACKGROUND ? GL_SRC_ALPHA
		: mode == MH_TRON_SOURCE_ALPHA	 ? GL_ONE_MINUS_SRC_ALPHA
		: mode == MH_TRON_ADD		 ? GL_ONE
						: GL_ZERO;
	glBlendFuncSeparate(source, destination, GL_ONE, GL_ZERO);
}

static void
draw_face(const struct mh_webgl_tron_draw *draw,
	const struct mh_tron_style *style)
{
	float material[4];

	material[0] = (float)style->fog;
	material[1] = (float)style->texture_alpha;
	material[2] = (float)style->vertex_alpha;
	material[3] = 0;
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(material), material);
	glDrawArrays(GL_TRIANGLES, (GLint)draw->range.fill_first,
		(GLsizei)draw->range.fill_count);

	if (draw->range.outline_count != 0) {
		material[3] = (float)style->outline_bias;
		glBufferSubData(
			GL_UNIFORM_BUFFER, 0, sizeof(material), material);
		glDrawArrays(GL_LINES, (GLint)draw->range.outline_first,
			(GLsizei)draw->range.outline_count);
	}
}

enum mh_error_code
mh_webgl_tron_draw(const struct mh_webgl_tron *tron,
	const struct mh_webgl_target *target, struct mh_error *error_out)
{
	const struct mh_tron_style *style, *previous;
	GLuint texture;
	size_t i;

	if (tron == NULL || target == NULL || target->framebuffer == 0 ||
		target->depth.handle == 0 ||
		target->color.width != tron->width ||
		target->color.height != tron->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL TRON target");

	if (tron->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	draw_state(tron, target);
	previous = NULL;
	texture = 0;

	for (i = 0; i < tron->count; i++) {
		style = mh_tron_style(tron->draws[i].range.pass);

		if (style != previous) {
			blend(style->blend);
			previous = style;
		}

		if (texture != tron->draws[i].texture) {
			texture = tron->draws[i].texture;
			glBindTexture(GL_TEXTURE_2D, texture);
		}

		draw_face(&tron->draws[i], style);
	}

	return mh_webgl_check_error(error_out);
}

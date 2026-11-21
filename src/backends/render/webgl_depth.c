#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_depth.h"
#include "backends/render/webgl_error.h"
#include "backends/render/webgl_presentation.h"
#include "backends/render/webgl_sampler.h"

enum {
	PROBES_MAX = 100000,
	RESULT_ROW_PIXELS = 1024
};

static enum mh_error_code
validate_probes(const struct mh_webgl_target *source,
	const struct mh_depth_probe *probes, size_t count, int *active,
	struct mh_error *error_out)
{
	const struct mh_depth_probe *probe;
	size_t i;

	if (source == NULL || source->framebuffer == 0 ||
		source->depth.handle == 0 ||
		source->depth.format != GL_DEPTH_COMPONENT32F ||
		count > PROBES_MAX || (count != 0 && probes == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL depth queries or source");

	*active = 0;

	for (i = 0; i < count; i++) {
		probe = &probes[i];

		if (probe->active != 0 && probe->active != 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL depth query flag");

		if (!probe->active)
			continue;

		if (!(probe->x > 0 && probe->x < source->depth.width &&
			probe->y > 0 && probe->y < source->depth.height &&
			probe->depth >= 0 && probe->depth <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL depth query position");

		*active = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_resources(struct mh_webgl_depth *depth, struct mh_error *error_out)
{
	size_t offset;
	enum mh_error_code status;

	if (depth->program.handle != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_webgl_program_create(
		&depth->program, MH_WEBGL_DEPTH, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_sampler_create(&depth->sampler,
			MH_TEXTURE_FILTER_NEAREST, 0, 0, error_out);

	if (status == MH_ERROR_NONE) {
		glGenBuffers(1, &depth->vertices);
		glBindBuffer(GL_ARRAY_BUFFER, depth->vertices);
		glGenBuffers(1, &depth->parameters);
		glBindBuffer(GL_UNIFORM_BUFFER, depth->parameters);
		status = mh_webgl_check_error(error_out);
	}

	if (status == MH_ERROR_NONE) {
		offset = 0;
		status = mh_webgl_vertex_array_create(&depth->array,
			MH_GPU_DEPTH_VERTEX, &depth->vertices, &offset,
			error_out);
	}

	if (status != MH_ERROR_NONE)
		mh_webgl_depth_destroy(depth);

	return status;
}

static enum mh_error_code
reserve_queries(
	struct mh_webgl_depth *depth, size_t count, struct mh_error *error_out)
{
	struct mh_webgl_target target;
	float *queries;
	unsigned char *active;
	unsigned int width, height;
	enum mh_error_code status;

	if (count <= depth->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	queries = (float *)malloc(count * 3 * sizeof(float));
	active = (unsigned char *)malloc(count);

	if (queries == NULL || active == NULL) {
		free(queries);
		free(active);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL depth queries");
	}

	width = count < RESULT_ROW_PIXELS ? (unsigned int)count
					: RESULT_ROW_PIXELS;
	height = (unsigned int)((count + width - 1) / width);
	memset(&target, 0, sizeof(target));
	status = mh_webgl_target_create(&target, width, height, 0, error_out);

	if (status != MH_ERROR_NONE) {
		free(queries);
		free(active);
		return status;
	}

	mh_webgl_target_destroy(&depth->results);
	free(depth->queries);
	free(depth->active);
	depth->results = target;
	depth->queries = queries;
	depth->active = active;
	depth->capacity = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
upload_queries(struct mh_webgl_depth *depth,
	const struct mh_webgl_target *source,
	const struct mh_depth_probe *probes, size_t count)
{
	float parameters[4];
	size_t i;

	for (i = 0; i < count; i++) {
		depth->active[i] = (unsigned char)probes[i].active;
		depth->queries[i * 3] = probes[i].active ? probes[i].x : 0;
		depth->queries[i * 3 + 1] = probes[i].active ? probes[i].y : 0;
		depth->queries[i * 3 + 2] =
			probes[i].active ? probes[i].depth : 0;
	}

	parameters[0] = (float)depth->results.color.width;
	parameters[1] = (float)depth->results.color.height;
	parameters[2] = (float)source->depth.height;
	parameters[3] = 0;
	glBindBuffer(GL_ARRAY_BUFFER, depth->vertices);
	glBufferData(GL_ARRAY_BUFFER, count * 3 * sizeof(float), depth->queries,
		GL_STREAM_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, depth->parameters);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(parameters), parameters,
		GL_STREAM_DRAW);
}

static enum mh_error_code
draw_queries(struct mh_webgl_depth *depth, const struct mh_webgl_target *source,
	size_t count, struct mh_error *error_out)
{
	GLint framebuffer, viewport[4];
	GLboolean scissor;

	/*
	 * WebGL cannot read depth pixels directly. Snapshot each comparison
	 * into RGBA8 now; reading this small target on the next frame preserves
	 * the native visibility delay without downloading the whole depth map.
	 */
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
	glGetIntegerv(GL_VIEWPORT, viewport);
	scissor = glIsEnabled(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depth->results.framebuffer);
	glViewport(0, 0, (GLsizei)depth->results.color.width,
		(GLsizei)depth->results.color.height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(depth->program.handle);
	glBindVertexArray(depth->array.handle);
	glBindBufferBase(GL_UNIFORM_BUFFER, MH_WEBGL_VERTEX_PARAMETERS,
		depth->parameters);
	glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
	glBindTexture(GL_TEXTURE_2D, source->depth.handle);
	glBindSampler(MH_WEBGL_TEXTURE, depth->sampler);
	glDrawArrays(GL_POINTS, 0, (GLsizei)count);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)framebuffer);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

	if (scissor)
		glEnable(GL_SCISSOR_TEST);

	return mh_webgl_check_error(error_out);
}

enum mh_error_code
mh_webgl_depth_queue(struct mh_webgl_depth *depth,
	const struct mh_webgl_target *source,
	const struct mh_depth_probe *probes, size_t count,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int active;

	if (depth == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL depth queries");

	if (depth->count != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"unread WebGL depth queries");

	status = validate_probes(source, probes, count, &active, error_out);

	if (status != MH_ERROR_NONE || !active)
		return status;

	status = create_resources(depth, error_out);

	if (status == MH_ERROR_NONE)
		status = reserve_queries(depth, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	upload_queries(depth, source, probes, count);
	status = draw_queries(depth, source, count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_download_queue(
			&depth->download, &depth->results, error_out);

	if (status == MH_ERROR_NONE)
		depth->count = count;

	return status;
}

static enum mh_error_code
read_queries(struct mh_webgl_depth *depth, unsigned char *visible, size_t count,
	int wait, struct mh_error *error_out)
{
	int ready;
	enum mh_error_code status;
	size_t i;

	if (depth == NULL || (count != 0 && visible == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL visibility output");

	if (depth->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (count != depth->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"WebGL visibility history size changed");

	status = mh_webgl_download_read(
		&depth->download, wait, &ready, error_out);

	if (status != MH_ERROR_NONE || !ready)
		return status;

	for (i = 0; i < count; i++)
		if (depth->active[i])
			visible[i] = (unsigned char)(depth->download.image
							.rgba[i * 4] != 0);

	depth->count = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_depth_read(struct mh_webgl_depth *depth, unsigned char *visible,
	size_t count, struct mh_error *error_out)
{
	return read_queries(depth, visible, count, 1, error_out);
}

enum mh_error_code
mh_webgl_depth_poll(struct mh_webgl_depth *depth, unsigned char *visible,
	size_t count, struct mh_error *error_out)
{
	return read_queries(depth, visible, count, 0, error_out);
}

void
mh_webgl_depth_destroy(struct mh_webgl_depth *depth)
{
	GLint active;

	if (depth == NULL)
		return;

	glGetIntegerv(GL_CURRENT_PROGRAM, &active);

	if ((GLuint)active == depth->program.handle)
		glUseProgram(0);

	mh_webgl_download_destroy(&depth->download);
	mh_webgl_program_destroy(&depth->program);
	mh_webgl_vertex_array_destroy(&depth->array);
	mh_webgl_target_destroy(&depth->results);
	glDeleteBuffers(1, &depth->vertices);
	glDeleteBuffers(1, &depth->parameters);
	glDeleteSamplers(1, &depth->sampler);
	free(depth->queries);
	free(depth->active);
	depth->vertices = depth->parameters = depth->sampler = 0;
	depth->queries = NULL;
	depth->active = NULL;
	depth->capacity = depth->count = 0;
}

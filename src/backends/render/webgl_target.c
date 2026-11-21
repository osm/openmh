#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_target.h"

static enum mh_error_code
attach(struct mh_webgl_target *target, struct mh_error *error_out)
{
	GLint previous_draw, previous_read;
	GLenum complete;
	enum mh_error_code status;

	previous_draw = previous_read = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
	glGenFramebuffers(1, &target->framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, target->color.handle, 0);

	if (target->depth.handle != 0)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, target->depth.handle, 0);

	complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (complete != GL_FRAMEBUFFER_COMPLETE || target->framebuffer == 0)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebGL render target is incomplete");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_target_create(struct mh_webgl_target *output, unsigned int width,
	unsigned int height, int depth, struct mh_error *error_out)
{
	struct mh_webgl_target target;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL render target output");

	if (output->framebuffer != 0 || output->color.handle != 0 ||
		output->depth.handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL render target is already created");

	memset(&target, 0, sizeof(target));
	status = mh_webgl_texture_create(
		&target.color, width, height, 1, GL_RGBA8, error_out);

	if (status == MH_ERROR_NONE && depth)
		status = mh_webgl_texture_create(&target.depth, width, height,
			1, GL_DEPTH_COMPONENT32F, error_out);

	if (status == MH_ERROR_NONE)
		status = attach(&target, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_target_destroy(&target);
		return status;
	}

	*output = target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_target_destroy(struct mh_webgl_target *target)
{
	if (target == NULL)
		return;

	glDeleteFramebuffers(1, &target->framebuffer);
	mh_webgl_texture_destroy(&target->color);
	mh_webgl_texture_destroy(&target->depth);
	target->framebuffer = 0;
}

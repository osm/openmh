#include <limits.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_presentation.h"

enum mh_error_code
mh_webgl_present(const struct mh_webgl_target *source, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	GLint previous_read, previous_draw;
	GLboolean scissor;

	if (source == NULL || source->framebuffer == 0 || width == 0 ||
		height == 0 || width > INT_MAX || height > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL presentation target");

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
	scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, source->framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	/*
	 * Both framebuffers use bottom-up coordinates; no texture-V inversion.
	 */
	glBlitFramebuffer(0, 0, (GLint)source->color.width,
		(GLint)source->color.height, 0, 0, (GLint)width, (GLint)height,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw);

	if (scissor)
		glEnable(GL_SCISSOR_TEST);

	return mh_webgl_check_error(error_out);
}

#include <GLES3/gl3.h>

#include "backends/render/webgl_error.h"

void
mh_webgl_discard_lost_object_errors(void)
{
	while (glGetError() != GL_NO_ERROR) {
	}
}

enum mh_error_code
mh_webgl_check_error(struct mh_error *error_out)
{
	GLenum error;

	error = glGetError();

	if (error == GL_NO_ERROR)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (error == GL_OUT_OF_MEMORY)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "WebGL allocation failed");

	return mh_error_set(
		error_out, MH_ERROR_BACKEND, "WebGL resource operation failed");
}

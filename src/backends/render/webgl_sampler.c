#include "backends/render/webgl_error.h"
#include "backends/render/webgl_sampler.h"
#include "engine/render/mipmap.h"

enum mh_error_code
mh_webgl_sampler_create(GLuint *output, enum mh_texture_filter filter,
	int repeat_u, int repeat_v, struct mh_error *error_out)
{
	GLuint sampler;
	GLint minification, magnification;
	enum mh_error_code status;

	if (output == NULL || filter < MH_TEXTURE_FILTER_NEAREST ||
		filter > MH_TEXTURE_FILTER_TRILINEAR)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL sampler request");

	if (*output != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL sampler is already created");

	magnification =
		filter == MH_TEXTURE_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
	minification = filter == MH_TEXTURE_FILTER_TRILINEAR
		? GL_LINEAR_MIPMAP_LINEAR
		: magnification;
	sampler = 0;
	glGenSamplers(1, &sampler);
	glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minification);
	glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magnification);
	glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S,
		repeat_u ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T,
		repeat_v ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, 0);
	glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD,
		filter == MH_TEXTURE_FILTER_TRILINEAR
			? MH_TEXTURE_MIP_LEVELS - 1
			: 0);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE || sampler == 0) {
		glDeleteSamplers(1, &sampler);
		return status != MH_ERROR_NONE
			? status
			: mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot create WebGL sampler");
	}

	*output = sampler;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

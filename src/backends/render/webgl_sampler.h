#ifndef MH_WEBGL_SAMPLER_H
#define MH_WEBGL_SAMPLER_H

#include <GLES3/gl3.h>

#include "interfaces/render/renderer.h"

/*
 * Output starts at zero and owns the returned sampler. Resolve ORIGINAL through
 * the selected profile first. Repeat flags independently control U and V.
 */
enum mh_error_code	mh_webgl_sampler_create(GLuint *output,
	enum mh_texture_filter filter, int repeat_u, int repeat_v,
	struct mh_error *error_out);

#endif

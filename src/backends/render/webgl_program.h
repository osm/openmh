#ifndef MH_WEBGL_PROGRAM_H
#define MH_WEBGL_PROGRAM_H

#include <GLES3/gl3.h>

#include "engine/core/error.h"

enum mh_webgl_program_kind {
	MH_WEBGL_MATERIAL,
	MH_WEBGL_MATERIAL_GLENZ,
	MH_WEBGL_DIRECT3D,
	MH_WEBGL_DIRECT3D_GLENZ,
	MH_WEBGL_POWERVR,
	MH_WEBGL_POWERVR_GLENZ,
	MH_WEBGL_UI,
	MH_WEBGL_TRON,
	MH_WEBGL_RETRO,
	MH_WEBGL_DEPTH,
	MH_WEBGL_OPENMH,
	MH_WEBGL_PROGRAM_COUNT
};

/* WebGL has one binding namespace, unlike the native per-stage slots. */
enum {
	MH_WEBGL_VERTEX_PARAMETERS = 0,
	MH_WEBGL_VERTEX_MATERIAL = 1,
	MH_WEBGL_FRAGMENT_MATERIAL = 2,
	MH_WEBGL_LIGHTING = 3,
	MH_WEBGL_SURFACE = 4,
	MH_WEBGL_TEXTURE = 0
};

struct mh_webgl_program {
	GLuint	handle;
	char	diagnostic[1024];
};

/*
 * Caller zero-initializes storage and keeps its WebGL context current.
 * Failure releases partial resources; diagnostics borrow this object's storage
 * until its next create call. A live program cannot be overwritten.
 * Uniform buffers and textures belong to the renderer, not the program.
 */
enum mh_error_code	mh_webgl_program_create(
	struct mh_webgl_program *program, enum mh_webgl_program_kind kind,
	struct mh_error *error_out);
void			mh_webgl_program_destroy(
	struct mh_webgl_program *program);

int	mh_webgl_program_available(enum mh_webgl_program_kind kind);

#endif

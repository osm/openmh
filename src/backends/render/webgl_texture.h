#ifndef MH_WEBGL_TEXTURE_H
#define MH_WEBGL_TEXTURE_H

#include <GLES3/gl3.h>

#include "engine/assets/image.h"

struct mh_webgl_texture {
	GLuint		handle;
	GLenum		format;
	unsigned int	width, height, levels;
};

/*
 * Zero-initialize output; failed creation leaves it unchanged. Owns immutable
 * RGBA8 or D32F storage. Caller keeps the owning context current through
 * destroy. Creation/upload restore texture binding and pixel-unpack state.
 */
enum mh_error_code	mh_webgl_texture_create(struct mh_webgl_texture *output,
	unsigned int width, unsigned int height, unsigned int levels,
	GLenum format, struct mh_error *error_out);

/*
 * Upload tightly packed RGBA8 into an existing level; source pixels are
 * borrowed for this call. Failure after submission may have changed the texture
 * contents.
 */
enum mh_error_code	mh_webgl_texture_upload(
	const struct mh_webgl_texture *texture, unsigned int level,
	const struct mh_image *image, struct mh_error *error_out);

/*
 * Own a complete RGBA8 asset texture, using the native mip reduction rules.
 * A partial failure releases all newly allocated storage. PowerVR callers first
 * use mh_powervr_texture_prepare and request one level.
 */
enum mh_error_code	mh_webgl_texture_from_image(
	struct mh_webgl_texture *output, const struct mh_image *image,
	unsigned int original_format, unsigned int levels,
	struct mh_error *error_out);
void			mh_webgl_texture_destroy(
	struct mh_webgl_texture *texture);

#endif

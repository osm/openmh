#ifndef MH_WEBGL_TARGET_H
#define MH_WEBGL_TARGET_H

#include "backends/render/webgl_texture.h"

struct mh_webgl_target {
	GLuint			framebuffer;
	struct mh_webgl_texture	color, depth;
};

/*
 * Owns one RGBA8 color texture and optional D32F depth texture. Zero-initialize
 * output; failure leaves it unchanged. Creation restores read/draw bindings.
 * Color texels have framebuffer orientation, unlike top-down asset uploads.
 */
enum mh_error_code	mh_webgl_target_create(struct mh_webgl_target *output,
	unsigned int width, unsigned int height, int depth,
	struct mh_error *error_out);
void			mh_webgl_target_destroy(struct mh_webgl_target *target);

#endif
